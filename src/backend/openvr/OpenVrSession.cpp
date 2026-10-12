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

#include "OpenVrSession.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

#include <openvr.h>

#include "Config.hpp"
#include "Hid.hpp"
#include "Input.hpp"
#include "Logger.hpp"
#include "OpenVrManifest.hpp"
#include "OpenVrRuntime.hpp"
#include "OverlayRenderer.hpp"
#include "Task.hpp"
#include "Watch.hpp"
#include "framexr_config.h"

#if FRAMEXR_FEATURE_OSC
#include "Osc.hpp"
#endif

namespace {

constexpr const char* kSteamVrSection = "steamvr";

constexpr std::array<const char*, 3> kColorGainKeys{"hmdDisplayColorGainR", "hmdDisplayColorGainG", "hmdDisplayColorGainB"};

auto SplitExtensions(const std::string& text) -> std::vector<std::string> {
    std::vector<std::string> extensions;
    std::istringstream stream(text);
    for (std::string name; stream >> name;) {
        extensions.push_back(name);
    }
    return extensions;
}

auto InstanceExtensions() -> std::vector<std::string> {
    auto* compositor = vr::VRCompositor();
    const auto length = compositor->GetVulkanInstanceExtensionsRequired(nullptr, 0);
    std::string buffer(length, '\0');
    if (length > 0) {
        compositor->GetVulkanInstanceExtensionsRequired(buffer.data(), length);
    }
    return SplitExtensions(buffer.c_str());
}

auto DeviceExtensions(VkPhysicalDevice physical_device) -> std::vector<std::string> {
    auto* compositor = vr::VRCompositor();
    const auto length = compositor->GetVulkanDeviceExtensionsRequired(physical_device, nullptr, 0);
    std::string buffer(length, '\0');
    if (length > 0) {
        compositor->GetVulkanDeviceExtensionsRequired(physical_device, buffer.data(), length);
    }
    return SplitExtensions(buffer.c_str());
}

auto DescribeSelector(const OverlaySelector& selector) -> std::string {
    if (const auto* id = std::get_if<OverlayId>(&selector)) {
        return std::format("Id(OverlayID({}))", static_cast<uint64_t>(*id));
    }
    return std::format("Name(\"{}\")", std::get<std::string>(selector));
}

} // namespace

auto OpenVrSession::ReadIpd() -> std::optional<float> {
    vr::ETrackedPropertyError error = vr::TrackedProp_Success;
    const float ipd = vr::VRSystem()
                          ->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_UserIpdMeters_Float, &error);
    if (error != vr::TrackedProp_Success) {
        return std::nullopt;
    }
    return std::round(ipd * 10000.0f) * 0.1f;
}

auto OpenVrSession::ReadDisplayDensity() -> Eigen::Vector2f {
    uint32_t width = 0;
    uint32_t height = 0;
    vr::VRSystem()->GetRecommendedRenderTargetSize(&width, &height);
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
    vr::VRSystem()->GetProjectionRaw(vr::Eye_Left, &left, &right, &top, &bottom);
    if (right - left <= 0.0f || std::abs(bottom - top) <= 0.0f) {
        return Eigen::Vector2f::Zero();
    }
    return {static_cast<float>(width) / (right - left), static_cast<float>(height) / std::abs(bottom - top)};
}

auto OpenVrSession::Create(OpenVrRuntime& runtime, bool show_by_default) -> Result<std::unique_ptr<OpenVrSession>> {
    DeviceCreateInfo device_info;
    device_info.instance_extensions = InstanceExtensions();
    device_info.required_device_extensions = [](VkPhysicalDevice physical_device) -> std::vector<std::string> {
        return DeviceExtensions(physical_device);
    };
    ASSIGN_OR_RETURN(auto device, Device::Create(device_info));
    runtime.Retain(std::move(device));
    ASSIGN_OR_RETURN(auto app, AppState::Create());

    if (show_by_default) {
        g_tasks->EnqueueAt(SystemTask{ShowHideTask{}}, std::chrono::steady_clock::now() + std::chrono::seconds(1));
    }

    if (const auto ipd = ReadIpd()) {
        g_input->SetIpd(*ipd);
        LOG_INFO("IPD: {:.1f} mm", *ipd);
    }
    g_input->SetDisplayPixelsPerRadian(ReadDisplayDensity());
    LOG_INFO(
        "Display density: {:.0f} x {:.0f} px/rad",
        g_input->DisplayPixelsPerRadian().x(),
        g_input->DisplayPixelsPerRadian().y()
    );

    if (auto installed = OpenVrManifest::Install(); !installed) {
        LOG_DEBUG("{}", installed.error().Message());
    }

    ASSIGN_OR_RETURN(auto overlays, OverlayContainer::Create());

    RETURN_IF_ERROR(OpenVrInput::SetActionManifest());
    ASSIGN_OR_RETURN(auto input, OpenVrInput::Create());

    vr::ETrackedPropertyError error = vr::TrackedProp_Success;
    const float refresh_rate = vr::VRSystem()->GetFloatTrackedDeviceProperty(
        vr::k_unTrackedDeviceIndex_Hmd,
        vr::Prop_DisplayFrequency_Float,
        &error
    );
    if (error != vr::TrackedProp_Success) {
        return Unexpected("Failed to get HMD refresh rate");
    }
    LOG_INFO("HMD running @ {} Hz", refresh_rate);
    const auto frame_timeout = 2 * static_cast<uint32_t>(std::floor(1000.0f / refresh_rate));

    ASSIGN_OR_RETURN(auto lines, OpenVrLines::Create());

    auto session = std::unique_ptr<OpenVrSession>(
        new OpenVrSession(std::move(app), std::move(overlays), input, std::move(lines), frame_timeout)
    );
    return session;
}

OpenVrSession::OpenVrSession(
    std::unique_ptr<AppState> app,
    std::unique_ptr<OverlayContainer> overlays,
    OpenVrInput input,
    OpenVrLines lines,
    uint32_t frame_timeout
)
    : m_app(std::move(app)),
      m_overlays(std::move(overlays)),
      m_notifications(std::make_unique<NotificationManager>()),
      m_input(input),
      m_lines(std::move(lines)),
      m_frame_timeout(frame_timeout),
      m_next_device_update(std::chrono::steady_clock::now()) {
    m_notifications->RunDesktop();
    m_notifications->RunUdp();
    m_playspace.PlayspaceChanged();

    auto* watch = m_overlays->ByName(kWatchName);
    if (watch == nullptr) {
        FatalError("Watch overlay is missing");
    }
    m_watch_id = watch->State().Id();
    m_pointer_lines = {m_lines.Allocate(), m_lines.Allocate()};
    m_editor_lines = {m_lines.Allocate(), m_lines.Allocate()};
}

auto OpenVrSession::DataFor(OverlayId id) -> OpenVrOverlayData& {
    return m_overlay_data[id];
}

auto OpenVrSession::DestroyOverlay(const Overlay& overlay) -> void {
    if (const auto it = m_overlay_data.find(overlay.State().Id()); it != m_overlay_data.end()) {
        OpenVrOverlay::Destroy(overlay, it->second);
        m_overlay_data.erase(it);
    }
}

auto OpenVrSession::AdjustGain(ColorChannel channel, float delta) -> void {
    auto* settings = vr::VRSettings();
    std::array<float, 3> current{};
    for (size_t i = 0; i < current.size(); ++i) {
        vr::EVRSettingsError error = vr::VRSettingsError_None;
        current[i] = settings->GetFloat(kSteamVrSection, kColorGainKeys[i], &error);
        if (error != vr::VRSettingsError_None) {
            return;
        }
    }

    float min = current[0] + current[1] + current[2] < 0.11f ? 0.1f : 0.0f;
    const auto set = [&](size_t index) -> bool {
        vr::EVRSettingsError error = vr::VRSettingsError_None;
        settings->SetFloat(kSteamVrSection, kColorGainKeys[index], std::clamp(current[index] + delta, min, 1.0f), &error);
        return error == vr::VRSettingsError_None;
    };

    switch (channel) {
    case ColorChannel::R: set(0); break;
    case ColorChannel::G: set(1); break;
    case ColorChannel::B: set(2); break;
    case ColorChannel::All:
        min *= 0.3333f;
        if (set(0) && set(1)) {
            set(2);
        }
        break;
    }
}

auto OpenVrSession::PollEvents() -> bool {
    vr::VREvent_t event{};
    while (vr::VRSystem()->PollNextEvent(&event, sizeof(event))) {
        switch (event.eventType) {
        case vr::VREvent_Quit: LOG_WARN("Received quit event, shutting down."); return false;
        case vr::VREvent_TrackedDeviceActivated:
        case vr::VREvent_TrackedDeviceDeactivated:
        case vr::VREvent_TrackedDeviceUpdated: m_next_device_update = std::chrono::steady_clock::now(); break;
        case vr::VREvent_SeatedZeroPoseReset:
        case vr::VREvent_StandingZeroPoseReset:
        case vr::VREvent_ChaperoneUniverseHasChanged:
        case vr::VREvent_SceneApplicationChanged: m_playspace.PlayspaceChanged(); break;
        case vr::VREvent_IpdChanged:
            if (const auto ipd = ReadIpd()) {
                if (std::abs(*ipd - g_input->Ipd()) > 0.05f) {
                    LOG_INFO("IPD: {:.1f} mm -> {:.1f} mm", g_input->Ipd(), *ipd);
                    ToastOverlay::Show(
                        Toast{.topic = ToastTopic::IpdChange, .title = "IPD", .body = std::format("{:.1f} mm", *ipd)}
                    );
                }
                g_input->SetIpd(*ipd);
            }
            break;
        default: break;
        }
    }
    return true;
}

auto OpenVrSession::RunSystemTask(const SystemTask& task) -> void {
    std::visit(
        [&](const auto& system_task) -> void {
            using TaskType = std::decay_t<decltype(system_task)>;
            if constexpr (std::is_same_v<TaskType, ColorGainTask>) {
                AdjustGain(system_task.channel, system_task.delta);
            } else if constexpr (std::is_same_v<TaskType, FixFloorTask>) {
                m_playspace.FixFloor();
            } else if constexpr (std::is_same_v<TaskType, ResetPlayspaceTask>) {
                m_playspace.ResetOffset();
            } else if constexpr (std::is_same_v<TaskType, ShowHideTask>) {
                m_lines.MarkDirty();
                m_overlays->ShowHide();
            } else if constexpr (std::is_same_v<TaskType, RecenterTask>) {
                m_overlays->Recenter();
            } else {
                m_overlays->OpenEditor();
            }
        },
        task
    );
}

auto OpenVrSession::DispatchTasks(size_t frame) -> void {
    std::deque<Task> due;
    g_tasks->TakeDue(due);
    while (!due.empty()) {
        auto task = std::move(due.front());
        due.pop_front();
        std::visit(
            [&](auto& value) -> void {
                using TaskType = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<TaskType, ModifyOverlayTask>) {
                    if (auto* overlay = m_overlays->BySelector(value.selector)) {
                        const bool was_visible = overlay->State().WantVisible();
                        value.modify(overlay->State());
                        if (!was_visible && overlay->State().WantVisible()) {
                            m_overlays->PlaceShown(*overlay);
                        }
                    } else {
                        LOG_WARN("Overlay not found for task: {}", DescribeSelector(value.selector));
                    }
                } else if constexpr (std::is_same_v<TaskType, CreateOverlayTask>) {
                    if (m_overlays->BySelector(value.selector) != nullptr) {
                        return;
                    }
                    auto created = value.create();
                    if (!created) {
                        return;
                    }
                    created->first.SetBirthFrame(frame);
                    m_overlays->Add(std::make_unique<Overlay>(std::move(created->first), std::move(created->second)));
                } else if constexpr (std::is_same_v<TaskType, DropOverlayTask>) {
                    auto* overlay = m_overlays->BySelector(value.selector);
                    if (overlay != nullptr && overlay->State().BirthFrame() < frame) {
                        DestroyOverlay(*overlay);
                        m_overlays->Remove(value.selector);
                    }
                } else {
                    RunSystemTask(value);
                }
            },
            task
        );
    }
}

auto OpenVrSession::UpdateInput() -> void {
    g_input->PreUpdate();
    m_input.Update(m_playspace.Universe());
    g_input->PostUpdate();
}

auto OpenVrSession::RenderOverlays() -> Result<> {
    LOG_TRACE("Rendering frame");
    CommandBatch batch;
    for (auto& overlay : m_overlays->Overlays()) {
        if (!overlay->State().WantVisible()) {
            continue;
        }
        ASSIGN_OR_RETURN(const auto readiness, overlay->Backend()->ShouldRender());
        if (readiness != RenderReadiness::Should) {
            continue;
        }
        auto& data = DataFor(overlay->State().Id());
        ASSIGN_OR_RETURN(const auto allocated, OpenVrOverlay::EnsureImageAllocated(*overlay, data));
        if (!allocated) {
            continue;
        }
        ASSIGN_OR_RETURN(data.image_dirty, overlay->Backend()->Render(data.image, batch, 1.0f));
    }

    LOG_TRACE("Rendering overlays");
    RETURN_IF_ERROR(batch.Submit());

    for (auto& overlay : m_overlays->Overlays()) {
        OpenVrOverlay::AfterRender(m_playspace.Universe(), *overlay, DataFor(overlay->State().Id()));
    }
    return {};
}

auto OpenVrSession::Run(std::atomic<bool>& running) -> Result<> {
    while (true) {
        vr::VROverlay()->WaitFrameSync(m_frame_timeout);

        if (!running.load()) {
            LOG_WARN("Received shutdown signal.");
            break;
        }

        const size_t frame = m_frame_counter++;

        if (!PollEvents()) {
            break;
        }

        if (m_next_device_update <= std::chrono::steady_clock::now()) {
            m_input.UpdateDevices();
            m_next_device_update = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        }

        m_notifications->SubmitPending();

        ASSIGN_OR_RETURN(auto removed, m_overlays->Update());
        m_overlays->UpdateTexelFootprints();
        for (const auto& overlay : removed) {
            DestroyOverlay(*overlay);
        }

        DispatchTasks(frame);
        UpdateInput();

        for (auto& overlay : m_overlays->Overlays()) {
            overlay->State().AutoMovement();
        }
        m_overlays->UpdateEditor();

        if (auto* watch = m_overlays->ById(m_watch_id)) {
            Watch::Fade(*watch);
        }
        m_playspace.Update();

        const auto results = InputState::Interact();
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& pointer = g_input->Pointers()[i];
            m_lines.DrawFrom(
                m_pointer_lines[i],
                Pointer::LinePose(pointer.Pose(), results[i].target),
                results[i].length,
                static_cast<size_t>(pointer.Interaction().mode) + 1,
                g_input->Hmd()
            );
            if (const auto& haptics = results[i].haptics) {
                m_input.TriggerHaptics(i, *haptics);
            }
        }

        const auto guides = m_overlays->EditorLines();
        for (size_t i = 0; i < m_editor_lines.size(); ++i) {
            if (i < guides.size()) {
                m_lines.DrawFrom(m_editor_lines[i], guides[i].pose, guides[i].length, 0, g_input->Hmd());
            } else {
                m_lines.Hide(m_editor_lines[i]);
            }
        }

        g_hid->Commit();

        RETURN_IF_ERROR(m_lines.Update(m_playspace.Universe()));

        for (auto& overlay : m_overlays->Overlays()) {
            RETURN_IF_ERROR(OpenVrOverlay::AfterInput(*overlay, DataFor(overlay->State().Id())));
        }

#if FRAMEXR_FEATURE_OSC
        if (g_osc) {
            if (const auto sent = g_osc->SendParams(g_input->Devices()); !sent) {
                LOG_TRACE("OSC: {}", sent.error().Message());
            }
        }
#endif

        RETURN_IF_ERROR(RenderOverlays());
    }
    return {};
}
