// FrameXR
//
// This is a port of wlx-overlay-s from Rust to C++.
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
// Copyright (C) 2023-2025 galister and wlx-overlay-s contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 3.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Runtime.hpp"

#include <array>
#include <deque>
#include <format>
#include <numbers>
#include <type_traits>
#include <variant>

#include "CommandLine.hpp"
#include "Devices.hpp"
#include "HidProvider.hpp"
#include "Interaction.hpp"
#include "Logger.hpp"
#include "Toast.hpp"
#include "Watch.hpp"
#include "framexr_config.h"

#if FRAMEXR_FEATURE_OSC
#include "Osc.hpp"
#endif

#if FRAMEXR_FEATURE_OPENVR
#include "OpenVrRuntime.hpp"
#endif

#if FRAMEXR_FEATURE_OPENXR
#include "OpenXrRuntime.hpp"
#endif

namespace {

constexpr auto kDeviceUpdateInterval = std::chrono::seconds(30);

constexpr auto kPointerLines = std::to_array({Line::LeftPointer, Line::RightPointer});

constexpr auto kGuideLines = std::to_array({Line::LeftGuide, Line::RightGuide});

auto Describe(const OverlaySelector& selector) -> std::string {
    if (const auto* id = std::get_if<OverlayId>(&selector)) {
        return std::format("Id(OverlayID({}))", static_cast<uint64_t>(*id));
    }
    return std::format("Name(\"{}\")", std::get<std::string>(selector));
}

} // namespace

auto Runtime::Run(std::atomic<bool>& running) -> void {
#if FRAMEXR_FEATURE_OPENXR
    if (!g_command_line->OpenVr() && Drive(OpenXrRuntime::Connect(), running)) {
        return;
    }
#endif
#if FRAMEXR_FEATURE_OPENVR
    if (!g_command_line->OpenXr() && Drive(OpenVrRuntime::Connect(), running)) {
        return;
    }
#endif
    LOG_ERROR("No more backends to try");
}

auto Runtime::Drive(std::unique_ptr<Runtime> runtime, std::atomic<bool>& running) -> bool {
    if (!runtime) {
        return false;
    }
    auto result = runtime->Open();
    if (result) {
        result = runtime->Loop(running);
    }
    if (!result) {
        LOG_ERROR("Fatal: {}", result.error().Message());
    }
    return true;
}

auto Runtime::Uninstall() -> void {
#if FRAMEXR_FEATURE_OPENVR
    OpenVrRuntime::Uninstall();
#endif
}

auto Runtime::BeamPose(Eigen::Affine3f from, float length, const Eigen::Affine3f& hmd) -> Eigen::Affine3f {
    constexpr float kPi = std::numbers::pi_v<float>;
    from.translation() += from.linear() * -Eigen::Vector3f::UnitZ() * (length * 0.5f);
    Eigen::Affine3f beam = from * Eigen::AngleAxisf(-kPi * 0.5f, Eigen::Vector3f::UnitX());

    const Eigen::Vector3f to_hmd = hmd.translation() - from.translation();
    const auto sides = std::to_array<Eigen::Vector3f>({
        Eigen::Vector3f::UnitZ(),
        Eigen::Vector3f::UnitX(),
        -Eigen::Vector3f::UnitZ(),
        -Eigen::Vector3f::UnitX(),
    });
    size_t closest = 0;
    for (size_t i = 1; i < sides.size(); ++i) {
        if (to_hmd.dot(beam.linear() * sides[i]) > to_hmd.dot(beam.linear() * sides[closest])) {
            closest = i;
        }
    }
    return beam * Eigen::AngleAxisf(kPi * 0.5f * static_cast<float>(closest), Eigen::Vector3f::UnitY());
}

auto Runtime::Start() -> Result<> {
    ASSIGN_OR_RETURN(m_app, AppState::Create());
    g_devices->SetOnConnect([](const TrackedDevice& device) -> void {
        if (device.role != TrackedDeviceRole::None) {
            Toast::Show({.title = std::format("{} connected", Devices::RoleName(device.role))});
        }
    });
    g_devices->SetOnDisconnect([](const TrackedDevice& device) -> void {
        if (device.role != TrackedDeviceRole::None) {
            Toast::Show({.title = std::format("{} disconnected", Devices::RoleName(device.role))});
        }
    });
    if (g_command_line->Show()) {
        g_tasks->EnqueueAt(SystemTask{ShowHideTask{}}, std::chrono::steady_clock::now() + std::chrono::seconds(1));
    }
    ASSIGN_OR_RETURN(m_overlays, Container::Create());
    m_notifications = std::make_unique<Notifications>();
    m_notifications->RunDesktop();
    m_notifications->RunUdp();
    m_watch_id = WatchOverlay().Id();
    return {};
}

auto Runtime::Stop() -> void {
    m_notifications.reset();
    m_overlays.reset();
    m_app.reset();
}

auto Runtime::WatchOverlay() -> View& {
    auto* watch = m_watch_id == OverlayId{} ? m_overlays->ByName(kWatchName) : m_overlays->ById(m_watch_id);
    if (watch == nullptr) {
        FatalError("Watch overlay is missing");
    }
    return *watch;
}

auto Runtime::Loop(std::atomic<bool>& running) -> Result<> {
    while (true) {
        ASSIGN_OR_RETURN(const auto status, BeginFrame(running.load()));
        if (status == FrameStatus::Quit) {
            return {};
        }
        if (const auto now = std::chrono::steady_clock::now(); m_next_device_update <= now) {
            UpdateDevices();
            m_next_device_update = now + kDeviceUpdateInterval;
        }
        if (status == FrameStatus::Skip) {
            continue;
        }
        ++m_frame;

        m_notifications->SubmitPending();
        ASSIGN_OR_RETURN(auto removed, m_overlays->Update());
        m_overlays->UpdateTexelFootprints();
        for (auto& overlay : removed) {
            Release(std::move(overlay));
        }
        DispatchTasks();

        RETURN_IF_ERROR(UpdateInput());
        for (auto& view : m_overlays->Views()) {
            view->AutoMovement();
            view->Update();
        }
        m_overlays->UpdateEditor();
        Interact();
        g_hid->Commit();

#if FRAMEXR_FEATURE_OSC
        if (g_osc) {
            if (const auto sent = g_osc->SendParams(g_devices->All()); !sent) {
                LOG_TRACE("OSC: {}", sent.error().Message());
            }
        }
#endif
        RETURN_IF_ERROR(EndFrame());
    }
}

auto Runtime::Interact() -> void {
    const auto results = Interaction::Interact();
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& pointer = g_interaction->Pointers()[i];
        const auto mode = static_cast<size_t>(pointer.Interaction().mode) + 1;
        DrawLine(kPointerLines[i], LaserPointer::LinePose(pointer.Pose(), results[i].target), results[i].length, mode);
        if (const auto& haptics = results[i].haptics) {
            ApplyHaptics(i, *haptics);
        }
    }
    const auto guides = m_overlays->EditorLines();
    for (size_t i = 0; i < kGuideLines.size(); ++i) {
        if (i < guides.size()) {
            DrawLine(kGuideLines[i], guides[i].pose, guides[i].length, 0);
        } else {
            HideLine(kGuideLines[i]);
        }
    }
}

auto Runtime::DispatchTasks() -> void {
    std::deque<Task> due;
    g_tasks->TakeDue(due);
    for (auto& task : due) {
        std::visit(
            [this](auto& value) -> void {
                using TaskType = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<TaskType, ModifyOverlayTask>) {
                    auto* overlay = m_overlays->BySelector(value.selector);
                    if (overlay == nullptr) {
                        LOG_WARN("Overlay not found for task: {}", Describe(value.selector));
                        return;
                    }
                    const bool was_visible = overlay->WantVisible();
                    value.modify(*overlay);
                    if (!was_visible && overlay->WantVisible()) {
                        m_overlays->PlaceShown(*overlay);
                    }
                } else if constexpr (std::is_same_v<TaskType, CreateOverlayTask>) {
                    if (m_overlays->BySelector(value.selector) != nullptr) {
                        return;
                    }
                    if (auto created = value.create()) {
                        created->SetBirthFrame(m_frame);
                        m_overlays->Add(std::move(created));
                    }
                } else if constexpr (std::is_same_v<TaskType, DropOverlayTask>) {
                    const auto* overlay = m_overlays->BySelector(value.selector);
                    if (overlay == nullptr || overlay->BirthFrame() >= m_frame) {
                        return;
                    }
                    LOG_DEBUG("{}: destroy", overlay->Name());
                    Release(m_overlays->Remove(value.selector));
                } else {
                    std::visit(
                        [this](const auto& system) -> void {
                            using SystemType = std::decay_t<decltype(system)>;
                            if constexpr (std::is_same_v<SystemType, ShowHideTask>) {
                                m_overlays->ShowHide();
                            } else if constexpr (std::is_same_v<SystemType, RecenterTask>) {
                                m_overlays->Recenter();
                            } else if constexpr (std::is_same_v<SystemType, OpenEditorTask>) {
                                m_overlays->OpenEditor();
                            }
                        },
                        value
                    );
                    RunSystemTask(value);
                }
            },
            task
        );
    }
}
