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

#include "OpenVrBackend.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

#include <openvr.h>

#include "Hid.hpp"
#include "Logger.hpp"
#include "OpenVrManifest.hpp"
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

auto ToBackendError(const Error& error) -> std::unexpected<BackendError> {
    return std::unexpected(BackendError{.failure = BackendFailure::Fatal, .message = error.Message()});
}

} // namespace

OpenVrRuntime::~OpenVrRuntime() {
    vr::VR_Shutdown();
}

auto OpenVrBackend::Uninstall() -> void {
    vr::EVRInitError error = vr::VRInitError_None;
    vr::VR_Init(&error, vr::VRApplication_Overlay);
    if (error != vr::VRInitError_None) {
        LOG_ERROR("Uninstall failed: could not reach OpenVR");
        return;
    }
    const OpenVrRuntime runtime;
    if (auto result = OpenVrManifest::Uninstall(); !result) {
        LOG_DEBUG("{}", result.error().Message());
    }
}

auto OpenVrBackend::Run(std::atomic<bool>& running, bool show_by_default) -> BackendResult {
    vr::EVRInitError error = vr::VRInitError_None;
    vr::VR_Init(&error, vr::VRApplication_Overlay);
    if (error != vr::VRInitError_None) {
        LOG_WARN("Will not use OpenVR: Context init failed");
        return std::unexpected(BackendError{.failure = BackendFailure::NotSupported, .message = "backend not supported"});
    }
    const OpenVrRuntime runtime;
    LOG_INFO("Using OpenVR runtime");

    auto session = OpenVrSession::Create(show_by_default);
    if (!session) {
        return ToBackendError(session.error());
    }
    if (auto result = (*session)->Run(running); !result) {
        return ToBackendError(result.error());
    }
    LOG_WARN("OpenVR shutdown");
    return {};
}

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

auto OpenVrSession::Create(bool show_by_default) -> Result<std::unique_ptr<OpenVrSession>> {
    DeviceCreateInfo device_info;
    device_info.instance_extensions = InstanceExtensions();
    device_info.required_device_extensions = [](VkPhysicalDevice physical_device) -> std::vector<std::string> {
        return DeviceExtensions(physical_device);
    };
    ASSIGN_OR_RETURN(auto device, Device::Create(device_info));
    ASSIGN_OR_RETURN(auto app, AppState::Create(std::move(device)));

    if (show_by_default) {
        app->tasks.EnqueueAt(SystemTask{ShowHideTask{}}, std::chrono::steady_clock::now() + std::chrono::seconds(1));
    }

    if (const auto ipd = ReadIpd()) {
        app->input.ipd = *ipd;
        LOG_INFO("IPD: {:.1f} mm", *ipd);
    }
    app->display_pixels_per_radian = ReadDisplayDensity();
    LOG_INFO("Display density: {:.0f} x {:.0f} px/rad", app->display_pixels_per_radian.x(), app->display_pixels_per_radian.y());

    if (auto installed = OpenVrManifest::Install(); !installed) {
        LOG_DEBUG("{}", installed.error().Message());
    }

    ASSIGN_OR_RETURN(auto overlays, OverlayContainer::Create(*app));

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

    ASSIGN_OR_RETURN(auto lines, OpenVrLines::Create(*app->device));

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
    : app_(std::move(app)),
      overlays_(std::move(overlays)),
      notifications_(std::make_unique<NotificationManager>()),
      input_(input),
      lines_(std::move(lines)),
      frame_timeout_(frame_timeout),
      next_device_update_(std::chrono::steady_clock::now()) {
    notifications_->RunDbus();
    notifications_->RunUdp();
    playspace_.PlayspaceChanged();

    auto* watch = overlays_->ByName(kWatchName);
    if (watch == nullptr) {
        FatalError("Watch overlay is missing");
    }
    watch_id_ = watch->state.id;
    pointer_lines_ = {lines_.Allocate(), lines_.Allocate()};
}

auto OpenVrSession::DataFor(OverlayId id) -> OpenVrOverlayData& {
    return overlay_data_[id];
}

auto OpenVrSession::DestroyOverlay(const Overlay& overlay) -> void {
    if (const auto it = overlay_data_.find(overlay.state.id); it != overlay_data_.end()) {
        OpenVrOverlay::Destroy(overlay, it->second);
        overlay_data_.erase(it);
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
        case vr::VREvent_TrackedDeviceUpdated: next_device_update_ = std::chrono::steady_clock::now(); break;
        case vr::VREvent_SeatedZeroPoseReset:
        case vr::VREvent_StandingZeroPoseReset:
        case vr::VREvent_ChaperoneUniverseHasChanged:
        case vr::VREvent_SceneApplicationChanged: playspace_.PlayspaceChanged(); break;
        case vr::VREvent_IpdChanged:
            if (const auto ipd = ReadIpd()) {
                if (std::abs(*ipd - app_->input.ipd) > 0.05f) {
                    LOG_INFO("IPD: {:.1f} mm -> {:.1f} mm", app_->input.ipd, *ipd);
                    Toast{.topic = ToastTopic::IpdChange, .title = "IPD", .body = std::format("{:.1f} mm", *ipd)}.Submit(*app_);
                }
                app_->input.ipd = *ipd;
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
                playspace_.FixFloor(app_->input);
            } else if constexpr (std::is_same_v<TaskType, ResetPlayspaceTask>) {
                playspace_.ResetOffset(app_->input);
            } else if constexpr (std::is_same_v<TaskType, ShowHideTask>) {
                lines_.MarkDirty();
                overlays_->ShowHide(*app_);
            } else {
                overlays_->Recenter(*app_);
            }
        },
        task
    );
}

auto OpenVrSession::DispatchTasks(size_t frame) -> void {
    std::deque<Task> due;
    app_->tasks.TakeDue(due);
    while (!due.empty()) {
        auto task = std::move(due.front());
        due.pop_front();
        std::visit(
            [&](auto& value) -> void {
                using TaskType = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<TaskType, ModifyOverlayTask>) {
                    if (auto* overlay = overlays_->BySelector(value.selector)) {
                        const bool was_visible = overlay->state.want_visible;
                        value.modify(*app_, overlay->state);
                        if (!was_visible && overlay->state.want_visible) {
                            overlays_->PlaceShown(*app_, *overlay);
                        }
                    } else {
                        LOG_WARN("Overlay not found for task: {}", DescribeSelector(value.selector));
                    }
                } else if constexpr (std::is_same_v<TaskType, CreateOverlayTask>) {
                    if (overlays_->BySelector(value.selector) != nullptr) {
                        return;
                    }
                    auto created = value.create(*app_);
                    if (!created) {
                        return;
                    }
                    created->first.birth_frame = frame;
                    overlays_->Add(std::make_unique<Overlay>(std::move(created->first), std::move(created->second)));
                } else if constexpr (std::is_same_v<TaskType, DropOverlayTask>) {
                    auto* overlay = overlays_->BySelector(value.selector);
                    if (overlay != nullptr && overlay->state.birth_frame < frame) {
                        DestroyOverlay(*overlay);
                        overlays_->Remove(value.selector);
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
    app_->input.PreUpdate();
    input_.Update(playspace_.Universe(), *app_);
    app_->input.PostUpdate(app_->config);
}

auto OpenVrSession::RenderOverlays() -> Result<> {
    LOG_TRACE("Rendering frame");
    CommandBatch batch;
    for (auto& overlay : overlays_->Overlays()) {
        if (!overlay->state.want_visible) {
            continue;
        }
        ASSIGN_OR_RETURN(const auto readiness, overlay->backend->ShouldRender(*app_));
        if (readiness != RenderReadiness::Should) {
            continue;
        }
        auto& data = DataFor(overlay->state.id);
        ASSIGN_OR_RETURN(const auto allocated, OpenVrOverlay::EnsureImageAllocated(*app_, *overlay, data));
        if (!allocated) {
            continue;
        }
        ASSIGN_OR_RETURN(data.image_dirty, overlay->backend->Render(*app_, data.image, batch, 1.0f));
    }

    LOG_TRACE("Rendering overlays");
    RETURN_IF_ERROR(batch.Submit());

    for (auto& overlay : overlays_->Overlays()) {
        OpenVrOverlay::AfterRender(playspace_.Universe(), *overlay, DataFor(overlay->state.id), *app_->device);
    }
    return {};
}

auto OpenVrSession::Run(std::atomic<bool>& running) -> Result<> {
    while (true) {
        vr::VROverlay()->WaitFrameSync(frame_timeout_);

        if (!running.load()) {
            LOG_WARN("Received shutdown signal.");
            break;
        }

        const size_t frame = frame_counter_++;

        if (!PollEvents()) {
            break;
        }

        if (next_device_update_ <= std::chrono::steady_clock::now()) {
            input_.UpdateDevices(*app_);
            next_device_update_ = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        }

        notifications_->SubmitPending(*app_);

        ASSIGN_OR_RETURN(auto removed, overlays_->Update(*app_));
        overlays_->UpdateTexelFootprints(*app_);
        for (const auto& overlay : removed) {
            DestroyOverlay(*overlay);
        }

        DispatchTasks(frame);
        UpdateInput();

        for (auto& overlay : overlays_->Overlays()) {
            overlay->state.AutoMovement(*app_);
        }

        if (auto* watch = overlays_->ById(watch_id_)) {
            Watch::Fade(*app_, *watch);
        }
        playspace_.Update(*overlays_, *app_);

        const auto results = InputState::Interact(*app_, *overlays_);
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& pointer = app_->input.pointers[i];
            lines_.DrawFrom(
                pointer_lines_[i],
                pointer.pose,
                results[i].length,
                static_cast<size_t>(pointer.interaction.mode) + 1,
                app_->input.hmd
            );
            if (const auto& haptics = results[i].haptics) {
                input_.TriggerHaptics(i, *haptics);
            }
        }

        app_->hid->Commit();

        RETURN_IF_ERROR(lines_.Update(playspace_.Universe(), *app_));

        for (auto& overlay : overlays_->Overlays()) {
            RETURN_IF_ERROR(OpenVrOverlay::AfterInput(*app_, *overlay, DataFor(overlay->state.id)));
        }

#if FRAMEXR_FEATURE_OSC
        if (app_->osc) {
            if (const auto sent = app_->osc->SendParams(*overlays_, app_->input.devices); !sent) {
                LOG_TRACE("OSC: {}", sent.error().Message());
            }
        }
#endif

        RETURN_IF_ERROR(RenderOverlays());
    }
    return {};
}
