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

#include "OpenXrBackend.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <variant>

#include "Hid.hpp"
#include "Logger.hpp"
#include "OpenXrPose.hpp"
#include "Watch.hpp"
#include "framexr_config.h"

#if FRAMEXR_FEATURE_OSC
#include "Osc.hpp"
#endif

namespace {

constexpr XrViewConfigurationType kViewType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

auto SessionStateName(XrSessionState state) -> std::string_view {
    switch (state) {
    case XR_SESSION_STATE_IDLE: return "IDLE";
    case XR_SESSION_STATE_READY: return "READY";
    case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
    case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
    case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
    case XR_SESSION_STATE_STOPPING: return "STOPPING";
    case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
    case XR_SESSION_STATE_EXITING: return "EXITING";
    default: return "UNKNOWN";
    }
}

auto BlendModeName(XrEnvironmentBlendMode mode) -> std::string_view {
    switch (mode) {
    case XR_ENVIRONMENT_BLEND_MODE_OPAQUE: return "OPAQUE";
    case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE: return "ADDITIVE";
    case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND: return "ALPHA_BLEND";
    default: return "UNKNOWN";
    }
}

auto DescribeSelector(const OverlaySelector& selector) -> std::string {
    if (const auto* id = std::get_if<OverlayId>(&selector)) {
        return std::format("Id(OverlayID({}))", static_cast<uint64_t>(*id));
    }
    return std::format("Name(\"{}\")", std::get<std::string>(selector));
}

auto LayerHeader(const OpenXrLayer& layer) -> const XrCompositionLayerBaseHeader* {
    return std::visit(
        [](const auto& value) -> const XrCompositionLayerBaseHeader* {
            return reinterpret_cast<const XrCompositionLayerBaseHeader*>(&value);
        },
        layer
    );
}

} // namespace

auto OpenXrSession::Create(const OpenXrInstance& instance, Device& device) -> Result<std::unique_ptr<OpenXrSession>> {
    ASSIGN_OR_RETURN(const auto handle, instance.CreateOverlaySession(device));
    auto session = std::unique_ptr<OpenXrSession>(new OpenXrSession(handle));
    ASSIGN_OR_RETURN(session->stage_, CreateReferenceSpace(instance, handle, XR_REFERENCE_SPACE_TYPE_STAGE));
    ASSIGN_OR_RETURN(session->view_, CreateReferenceSpace(instance, handle, XR_REFERENCE_SPACE_TYPE_VIEW));
    return session;
}

OpenXrSession::OpenXrSession(XrSession session) : session_(session) {
}

OpenXrSession::~OpenXrSession() {
    if (view_ != XR_NULL_HANDLE) {
        xrDestroySpace(view_);
    }
    if (stage_ != XR_NULL_HANDLE) {
        xrDestroySpace(stage_);
    }
    xrDestroySession(session_);
}

auto OpenXrSession::CreateReferenceSpace(const OpenXrInstance& instance, XrSession session, XrReferenceSpaceType type)
    -> Result<XrSpace> {
    const XrReferenceSpaceCreateInfo create_info{
        .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
        .referenceSpaceType = type,
        .poseInReferenceSpace = {.orientation = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f}, .position = {}},
    };
    XrSpace space = XR_NULL_HANDLE;
    RETURN_IF_ERROR(instance.Check(xrCreateReferenceSpace(session, &create_info, &space)));
    return space;
}

auto OpenXrBackend::Run(std::atomic<bool>& running, bool show_by_default) -> BackendResult {
    auto instance = OpenXrInstance::Create();
    if (!instance) {
        LOG_WARN("Will not use OpenXR: {}", instance.error().Message());
        return std::unexpected(BackendError{.failure = BackendFailure::NotSupported, .message = instance.error().Message()});
    }

    auto main_loop = OpenXrMainLoop::Create(std::move(*instance), show_by_default);
    if (!main_loop) {
        return std::unexpected(BackendError{.failure = BackendFailure::Fatal, .message = main_loop.error().Message()});
    }
    if (auto result = (*main_loop)->Run(running); !result) {
        return std::unexpected(BackendError{.failure = BackendFailure::Fatal, .message = result.error().Message()});
    }
    return {};
}

auto OpenXrMainLoop::Create(std::unique_ptr<OpenXrInstance> instance, bool show_by_default)
    -> Result<std::unique_ptr<OpenXrMainLoop>> {
    auto main_loop = std::unique_ptr<OpenXrMainLoop>(new OpenXrMainLoop(std::move(instance)));
    RETURN_IF_ERROR(main_loop->Initialize(show_by_default));
    return main_loop;
}

OpenXrMainLoop::OpenXrMainLoop(std::unique_ptr<OpenXrInstance> instance)
    : instance_(std::move(instance)),
      next_device_update_(std::chrono::steady_clock::now()) {
}

auto OpenXrMainLoop::Initialize(bool show_by_default) -> Result<> {
    const auto& instance = *instance_;

    DeviceCreateInfo device_info;
    device_info.create_instance = [&](const VkInstanceCreateInfo& create_info) -> Result<VkInstance> {
        return instance.CreateVulkanInstance(create_info);
    };
    device_info.select_physical_device = [&](VkInstance vk_instance) -> Result<VkPhysicalDevice> {
        return instance.VulkanGraphicsDevice(vk_instance);
    };
    device_info.create_device = [&](VkPhysicalDevice physical_device, const VkDeviceCreateInfo& create_info) -> Result<VkDevice> {
        return instance.CreateVulkanDevice(physical_device, create_info);
    };
    ASSIGN_OR_RETURN(auto device, Device::Create(device_info));
    ASSIGN_OR_RETURN(app_, AppState::Create(std::move(device)));

    ASSIGN_OR_RETURN(const auto modes, instance.EnvironmentBlendModes());
    if (modes.empty()) {
        return Unexpected("No environment blend modes available");
    }
    blend_mode_ = std::ranges::contains(modes, XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND) && app_->config.use_passthrough
                    ? XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND
                    : modes.front();
    LOG_INFO("Using environment blend mode: {}", BlendModeName(blend_mode_));

    if (show_by_default) {
        app_->tasks.EnqueueAt(SystemTask{ShowHideTask{}}, std::chrono::steady_clock::now() + std::chrono::seconds(1));
    }

    ASSIGN_OR_RETURN(overlays_, OverlayContainer::Create(*app_));
    ASSIGN_OR_RETURN(lines_, OpenXrLines::Create(*app_->device));

    notifications_ = std::make_unique<NotificationManager>();
    notifications_->RunDbus();
    notifications_->RunUdp();

    if (auto monado = Monado::AutoConnect()) {
        monado_ = std::move(*monado);
    } else {
        LOG_WARN("Will not use libmonado: {}", monado.error().Message());
    }

    if (monado_) {
        if (auto playspace = OpenXrPlayspace::Create(*monado_)) {
            playspace_ = std::move(*playspace);
        } else {
            LOG_WARN("Will not use Monado playspace mover: {}", playspace.error().Message());
        }
        blocker_.emplace();
    }

    ASSIGN_OR_RETURN(session_, OpenXrSession::Create(instance, *app_->device));
    xr_ = OpenXrState{
        .instance = instance_.get(),
        .session = session_->Handle(),
        .stage = session_->Stage(),
        .view = session_->View(),
    };

    if (blend_mode_ == XR_ENVIRONMENT_BLEND_MODE_OPAQUE) {
        skybox_ = OpenXrSkybox::Create(xr_, *app_);
    }

    for (auto& line : pointer_lines_) {
        ASSIGN_OR_RETURN(line, lines_->Allocate(xr_, *app_->device));
    }

    const auto* watch = overlays_->ByName(kWatchName);
    if (watch == nullptr) {
        FatalError("Watch overlay is missing");
    }
    watch_id_ = watch->state.id;

    ASSIGN_OR_RETURN(input_, OpenXrInput::Create(xr_));
    return {};
}

auto OpenXrMainLoop::WatchOverlay() -> Overlay& {
    auto* watch = overlays_->ById(watch_id_);
    if (watch == nullptr) {
        FatalError("Watch overlay is missing");
    }
    return *watch;
}

auto OpenXrMainLoop::HandleVisibility(bool visible) -> void {
    if (main_session_visible_ == visible) {
        return;
    }
    main_session_visible_ = visible;
    LOG_INFO("Main session visible: {}", main_session_visible_);
    if (main_session_visible_) {
        LOG_DEBUG("Destroying skybox.");
        skybox_.reset();
    } else if (blend_mode_ == XR_ENVIRONMENT_BLEND_MODE_OPAQUE) {
        LOG_DEBUG("Allocating skybox.");
        skybox_ = OpenXrSkybox::Create(xr_, *app_);
    }
}

auto OpenXrMainLoop::PollEvents() -> Result<bool> {
    while (true) {
        XrEventDataBuffer event{.type = XR_TYPE_EVENT_DATA_BUFFER};
        const auto result = xrPollEvent(instance_->Handle(), &event);
        RETURN_IF_ERROR(instance_->Check(result));
        if (result == XR_EVENT_UNAVAILABLE) {
            return true;
        }

        switch (event.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            const auto& changed = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
            LOG_INFO("entered state {}", SessionStateName(changed.state));
            switch (changed.state) {
            case XR_SESSION_STATE_READY: {
                const XrSessionBeginInfo begin_info{.type = XR_TYPE_SESSION_BEGIN_INFO, .primaryViewConfigurationType = kViewType};
                RETURN_IF_ERROR(instance_->Check(xrBeginSession(xr_.session, &begin_info)));
                session_running_ = true;
                break;
            }
            case XR_SESSION_STATE_STOPPING:
                RETURN_IF_ERROR(instance_->Check(xrEndSession(xr_.session)));
                session_running_ = false;
                break;
            case XR_SESSION_STATE_EXITING:
            case XR_SESSION_STATE_LOSS_PENDING: return false;
            default: break;
            }
            break;
        }
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: return false;
        case XR_TYPE_EVENT_DATA_EVENTS_LOST:
            LOG_WARN("lost {} events", reinterpret_cast<const XrEventDataEventsLost*>(&event)->lostEventCount);
            break;
        case XR_TYPE_EVENT_DATA_MAIN_SESSION_VISIBILITY_CHANGED_EXTX:
            HandleVisibility(reinterpret_cast<const XrEventDataMainSessionVisibilityChangedEXTX*>(&event)->visible != XR_FALSE);
            break;
        default: break;
        }
    }
}

auto OpenXrMainLoop::UpdateFps() -> float {
    const auto now = std::chrono::steady_clock::now();
    fps_counter_.push_back(now);
    while (!fps_counter_.empty() && std::chrono::duration<float>(now - fps_counter_.front()).count() > 1.0f) {
        fps_counter_.pop_front();
    }
    const float total_elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - fps_counter_.front()).count();
    return static_cast<float>(fps_counter_.size()) / total_elapsed;
}

auto OpenXrMainLoop::EndFrame(std::span<const XrCompositionLayerBaseHeader* const> layers) -> Result<> {
    const XrFrameEndInfo end_info{
        .type = XR_TYPE_FRAME_END_INFO,
        .displayTime = xr_.predicted_display_time,
        .environmentBlendMode = blend_mode_,
        .layerCount = static_cast<uint32_t>(layers.size()),
        .layers = layers.data(),
    };
    return instance_->Check(xrEndFrame(xr_.session, &end_info));
}

auto OpenXrMainLoop::UpdateInput() -> Result<> {
    app_->input.PreUpdate();
    RETURN_IF_ERROR(input_->Update(xr_, *app_));
    app_->input.PostUpdate(app_->config);

    if (blocker_) {
        blocker_->Update(*app_, watch_id_, *monado_);
    }

    const auto pressed = [&](auto member) -> bool {
        return std::ranges::any_of(app_->input.pointers, [&](const Pointer& pointer) -> bool {
            return pointer.now.*member && !(pointer.before.*member);
        });
    };
    if (pressed(&PointerState::show_hide)) {
        overlays_->ShowHide(*app_);
    }

    Watch::Fade(*app_, WatchOverlay());
    if (playspace_) {
        playspace_->Update(*overlays_, *app_, *monado_);
    }

    for (auto& overlay : overlays_->Overlays()) {
        RETURN_IF_ERROR(overlay_data_[overlay->state.id].AfterInput(*overlay, *app_));
    }

#if FRAMEXR_FEATURE_OSC
    if (app_->osc) {
        if (const auto sent = app_->osc->SendParams(*overlays_, app_->input.devices); !sent) {
            LOG_TRACE("OSC: {}", sent.error().Message());
        }
    }
#endif
    return {};
}

auto OpenXrMainLoop::UpdateIpd(XrTime display_time) -> Result<> {
    const XrViewLocateInfo locate_info{
        .type = XR_TYPE_VIEW_LOCATE_INFO,
        .viewConfigurationType = kViewType,
        .displayTime = display_time,
        .space = xr_.stage,
    };
    XrViewState view_state{.type = XR_TYPE_VIEW_STATE};
    std::array<XrView, 2> views{XrView{.type = XR_TYPE_VIEW}, XrView{.type = XR_TYPE_VIEW}};
    uint32_t count = 0;
    RETURN_IF_ERROR(instance_->Check(
        xrLocateViews(xr_.session, &locate_info, &view_state, static_cast<uint32_t>(views.size()), &count, views.data())
    ));

    const float ipd = OpenXrPose::Ipd(std::span(views).first(count));
    if (std::abs(app_->input.ipd - ipd) > 0.01f) {
        LOG_INFO("IPD changed: {} -> {}", app_->input.ipd, ipd);
        app_->input.ipd = ipd;
        Toast{.topic = ToastTopic::IpdChange, .title = "IPD", .body = std::format("{:.1f} mm", ipd)}.Submit(*app_);
    }
    return {};
}

auto OpenXrMainLoop::RenderOverlays() -> Result<> {
    CommandBatch batch;

    if (!main_session_visible_ && skybox_) {
        RETURN_IF_ERROR(skybox_->Render(xr_, *app_, batch));
    }

    for (auto& overlay : overlays_->Overlays()) {
        auto& data = overlay_data_[overlay->state.id];
        data.cur_visible = false;
        if (!overlay->state.want_visible) {
            continue;
        }

        if (!data.init) {
            RETURN_IF_ERROR(overlay->Init(*app_));
            data.init = true;
        }

        ASSIGN_OR_RETURN(const auto readiness, overlay->backend->ShouldRender(*app_));
        const bool should_render = readiness == RenderReadiness::Should
                                || (readiness == RenderReadiness::Can
                                    && std::abs(data.last_alpha - overlay->state.alpha) > std::numeric_limits<float>::epsilon());

        if (should_render) {
            ASSIGN_OR_RETURN(const bool ready, data.EnsureSwapchain(*overlay, *app_, xr_));
            if (!ready) {
                continue;
            }
            ASSIGN_OR_RETURN(const auto target, data.swapchain->AcquireWaitImage());
            ASSIGN_OR_RETURN(const bool rendered, overlay->backend->Render(*app_, target, batch, overlay->state.alpha));
            if (!rendered) {
                RETURN_IF_ERROR(data.swapchain->EnsureImageReleased());
                continue;
            }
            data.last_alpha = overlay->state.alpha;
        } else if (!data.swapchain) {
            continue;
        }
        data.cur_visible = true;
    }

    RETURN_IF_ERROR(lines_->Render(*app_->device, batch));
    return batch.Submit();
}

auto OpenXrMainLoop::ComposeLayers() -> Result<std::vector<std::pair<float, OpenXrLayer>>> {
    std::vector<std::pair<float, OpenXrLayer>> layers;
    if (!main_session_visible_ && skybox_) {
        ASSIGN_OR_RETURN(auto sky_layers, skybox_->Present(xr_, *app_));
        for (size_t i = 0; i < sky_layers.size(); ++i) {
            layers.emplace_back(std::fma(static_cast<float>(i), -50.0f, 200.0f), sky_layers[i]);
        }
    }

    for (auto& overlay : overlays_->Overlays()) {
        auto& data = overlay_data_[overlay->state.id];
        if (!data.cur_visible) {
            continue;
        }
        const float dist_sq = (app_->input.hmd.translation() - overlay->state.transform.translation()).squaredNorm()
                            + (100.0f - static_cast<float>(overlay->state.z_order));
        if (!std::isnormal(dist_sq)) {
            RETURN_IF_ERROR(data.swapchain->EnsureImageReleased());
            continue;
        }
        ASSIGN_OR_RETURN(auto layer, data.Present(*overlay, xr_));
        if (layer) {
            layers.emplace_back(dist_sq, *layer);
        }
    }

    ASSIGN_OR_RETURN(auto line_layers, lines_->Present(xr_));
    for (auto& layer : line_layers) {
        layers.emplace_back(0.0f, layer);
    }
    return layers;
}

auto OpenXrMainLoop::QueueDelete(std::unique_ptr<Overlay> overlay, size_t frame) -> void {
    OpenXrOverlay data;
    if (auto node = overlay_data_.extract(overlay->state.id)) {
        data = std::move(node.mapped());
    }
    delete_queue_.push_back(OpenXrPendingDelete{.overlay = std::move(overlay), .data = std::move(data), .frame = frame});
}

auto OpenXrMainLoop::RunSystemTask(const SystemTask& task) -> void {
    std::visit(
        [&](const auto& system_task) -> void {
            using TaskType = std::decay_t<decltype(system_task)>;
            if constexpr (std::is_same_v<TaskType, FixFloorTask>) {
                if (playspace_) {
                    playspace_->FixFloor(app_->input, *monado_);
                }
            } else if constexpr (std::is_same_v<TaskType, ResetPlayspaceTask>) {
                if (playspace_) {
                    playspace_->ResetOffset(*monado_);
                }
            } else if constexpr (std::is_same_v<TaskType, ShowHideTask>) {
                overlays_->ShowHide(*app_);
            } else if constexpr (std::is_same_v<TaskType, RecenterTask>) {
                overlays_->Recenter(*app_);
            }
        },
        task
    );
}

auto OpenXrMainLoop::DispatchTasks(size_t frame) -> void {
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
                    const auto* overlay = overlays_->BySelector(value.selector);
                    if (overlay == nullptr || overlay->state.birth_frame >= frame) {
                        return;
                    }
                    LOG_DEBUG("{}: destroy", overlay->state.name);
                    if (auto removed = overlays_->Remove(value.selector)) {
                        QueueDelete(std::move(removed), frame + 5);
                    }
                } else {
                    RunSystemTask(value);
                }
            },
            task
        );
    }
}

auto OpenXrMainLoop::Run(std::atomic<bool>& running) -> Result<> {
    while (true) {
        const size_t frame = frame_counter_++;

        if (!running.load()) {
            LOG_WARN("Received shutdown signal.");
            const auto result = xrRequestExitSession(xr_.session);
            if (result == XR_ERROR_SESSION_NOT_RUNNING) {
                break;
            }
            if (XR_FAILED(result)) {
                LOG_ERROR("Failed to request OpenXR session exit: {}", instance_->ResultName(result));
                break;
            }
            LOG_INFO("OpenXR session exit requested.");
        }

        ASSIGN_OR_RETURN(const bool keep_running, PollEvents());
        if (!keep_running) {
            break;
        }

        if (next_device_update_ <= std::chrono::steady_clock::now() && monado_) {
            OpenXrInput::UpdateDevices(*app_, *monado_);
            next_device_update_ = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        }

        if (!session_running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        XrFrameState frame_state{.type = XR_TYPE_FRAME_STATE};
        const XrFrameWaitInfo wait_info{.type = XR_TYPE_FRAME_WAIT_INFO};
        RETURN_IF_ERROR(instance_->Check(xrWaitFrame(xr_.session, &wait_info, &frame_state)));
        const XrFrameBeginInfo begin_info{.type = XR_TYPE_FRAME_BEGIN_INFO};
        RETURN_IF_ERROR(instance_->Check(xrBeginFrame(xr_.session, &begin_info)));

        xr_.predicted_display_time = frame_state.predictedDisplayTime;
        xr_.fps = UpdateFps();

        if (frame_state.shouldRender == XR_FALSE) {
            RETURN_IF_ERROR(EndFrame({}));
            continue;
        }

        RETURN_IF_ERROR(UpdateInput());
        RETURN_IF_ERROR(UpdateIpd(frame_state.predictedDisplayTime));

        for (auto& overlay : overlays_->Overlays()) {
            overlay->state.AutoMovement(*app_);
        }

        const auto results = InputState::Interact(*app_, *overlays_);
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& pointer = app_->input.pointers.at(i);
            lines_->DrawFrom(
                pointer_lines_.at(i),
                pointer.pose,
                results.at(i).length,
                static_cast<size_t>(pointer.interaction.mode) + 1,
                app_->input.hmd
            );
            if (const auto& haptics = results.at(i).haptics) {
                input_->ApplyHaptics(xr_, i, *haptics);
            }
        }

        app_->hid->Commit();

        auto& watch = WatchOverlay();
        const Eigen::Affine3f watch_transform = watch.state.transform;
        if (!watch.state.want_visible) {
            watch.state.want_visible = true;
            watch.state.transform = Eigen::Affine3f(Eigen::Scaling(0.001f));
        }

        RETURN_IF_ERROR(RenderOverlays());
        ASSIGN_OR_RETURN(auto layers, ComposeLayers());

        std::ranges::stable_sort(layers, [](const auto& a, const auto& b) -> bool { return a.first > b.first; });
        const auto headers = layers | std::views::transform([](const auto& layer) -> const XrCompositionLayerBaseHeader* {
                                 return LayerHeader(layer.second);
                             })
                           | std::ranges::to<std::vector>();
        RETURN_IF_ERROR(EndFrame(headers));

        ASSIGN_OR_RETURN(auto removed, overlays_->Update(*app_));
        for (auto& overlay : removed) {
            QueueDelete(std::move(overlay), frame + 5);
        }

        notifications_->SubmitPending(*app_);

        DispatchTasks(frame);

        std::erase_if(delete_queue_, [&](const OpenXrPendingDelete& pending) -> bool { return pending.frame <= frame; });

        WatchOverlay().state.transform = watch_transform;
    }
    return {};
}
