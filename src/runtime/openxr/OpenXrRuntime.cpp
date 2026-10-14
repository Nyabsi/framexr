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

#include "OpenXrRuntime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <variant>

#include "Config.hpp"
#include "Interaction.hpp"
#include "Logger.hpp"
#include "OpenXrPose.hpp"
#include "Compose.hpp"
#include "Toast.hpp"

namespace
{

constexpr XrViewConfigurationType kViewType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

auto DisplayDensity(const OpenXrInstance& instance, const XrFovf& fov) -> Eigen::Vector2f
{
    std::array<XrViewConfigurationView, 2> views{
        XrViewConfigurationView{.type = XR_TYPE_VIEW_CONFIGURATION_VIEW},
        XrViewConfigurationView{.type = XR_TYPE_VIEW_CONFIGURATION_VIEW},
    };
    uint32_t count = 0;
    const XrResult result = xrEnumerateViewConfigurationViews(
        instance.Handle(),
        instance.System(),
        kViewType,
        static_cast<uint32_t>(views.size()),
        &count,
        views.data()
    );
    const float horizontal = std::tan(fov.angleRight) - std::tan(fov.angleLeft);
    const float vertical = std::tan(fov.angleUp) - std::tan(fov.angleDown);
    if (XR_FAILED(result) || count == 0 || horizontal <= 0.0f || vertical <= 0.0f)
    {
        return Eigen::Vector2f::Zero();
    }
    return {
        static_cast<float>(views.front().recommendedImageRectWidth) / horizontal,
        static_cast<float>(views.front().recommendedImageRectHeight) / vertical,
    };
}

auto SessionStateName(XrSessionState state) -> std::string_view
{
    switch (state)
    {
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

auto BlendModeName(XrEnvironmentBlendMode mode) -> std::string_view
{
    switch (mode)
    {
    case XR_ENVIRONMENT_BLEND_MODE_OPAQUE: return "OPAQUE";
    case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE: return "ADDITIVE";
    case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND: return "ALPHA_BLEND";
    default: return "UNKNOWN";
    }
}

auto LayerHeader(const OpenXrLayer& layer) -> const XrCompositionLayerBaseHeader*
{
    return std::visit(
        [](const auto& value) -> const XrCompositionLayerBaseHeader*
        { return reinterpret_cast<const XrCompositionLayerBaseHeader*>(&value); },
        layer
    );
}

} // namespace

auto OpenXrRuntime::Connect() -> std::unique_ptr<OpenXrRuntime>
{
    auto instance = OpenXrInstance::Create();
    if (!instance)
    {
        LOG_WARN("Will not use OpenXR: {}", instance.error().Message());
        return nullptr;
    }
    return std::unique_ptr<OpenXrRuntime>(new OpenXrRuntime(std::move(*instance)));
}

OpenXrRuntime::OpenXrRuntime(std::unique_ptr<OpenXrInstance> instance) : m_instance(std::move(instance))
{
}

OpenXrRuntime::~OpenXrRuntime()
{
    m_delete_queue.clear();
    m_overlay_data.clear();
    m_input.reset();
    m_skybox.reset();
    m_lines.reset();
    m_session.reset();
    m_blocker.reset();
    m_playspace.reset();
    m_monado.reset();
    Stop();
}

auto OpenXrRuntime::Open() -> Result<>
{
    const auto& instance = *m_instance;

    DeviceCreateInfo device_info;
    device_info.create_instance = [&](const VkInstanceCreateInfo& create_info) -> Result<VkInstance>
    { return instance.CreateVulkanInstance(create_info); };
    device_info.select_physical_device = [&](VkInstance vk_instance) -> Result<VkPhysicalDevice>
    { return instance.VulkanGraphicsDevice(vk_instance); };
    device_info.create_device = [&](VkPhysicalDevice physical_device, const VkDeviceCreateInfo& create_info) -> Result<VkDevice>
    { return instance.CreateVulkanDevice(physical_device, create_info); };
    ASSIGN_OR_RETURN(m_device, Device::Create(device_info));
    RETURN_IF_ERROR(Start());

    ASSIGN_OR_RETURN(const auto modes, instance.EnvironmentBlendModes());
    if (modes.empty())
    {
        return Unexpected("No environment blend modes available");
    }
    m_blend_mode = std::ranges::contains(modes, XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND) && g_config->use_passthrough
                     ? XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND
                     : modes.front();
    LOG_INFO("Using environment blend mode: {}", BlendModeName(m_blend_mode));

    ASSIGN_OR_RETURN(m_lines, OpenXrLines::Create());

    if (auto monado = Monado::AutoConnect())
    {
        m_monado = std::move(*monado);
    }
    else
    {
        LOG_WARN("Will not use libmonado: {}", monado.error().Message());
    }

    if (m_monado)
    {
        if (auto playspace = OpenXrPlayspace::Create(*m_monado))
        {
            m_playspace = std::move(*playspace);
        }
        else
        {
            LOG_WARN("Will not use Monado playspace mover: {}", playspace.error().Message());
        }
        m_blocker.emplace();
    }

    ASSIGN_OR_RETURN(m_session, OpenXrSession::Create(instance));
    m_xr = OpenXrState{
        .instance = m_instance.get(),
        .session = m_session->Handle(),
        .stage = m_session->Stage(),
        .view = m_session->View(),
    };

    if (m_blend_mode == XR_ENVIRONMENT_BLEND_MODE_OPAQUE)
    {
        m_skybox = OpenXrSkybox::Create(m_xr);
    }

    for (auto& line : m_line_ids)
    {
        ASSIGN_OR_RETURN(line, m_lines->Allocate(m_xr));
    }

    ASSIGN_OR_RETURN(m_input, OpenXrInput::Create(m_xr));
    return {};
}

auto OpenXrRuntime::HandleVisibility(bool visible) -> void
{
    if (m_main_session_visible == visible)
    {
        return;
    }
    m_main_session_visible = visible;
    LOG_INFO("Main session visible: {}", m_main_session_visible);
    if (m_main_session_visible)
    {
        LOG_DEBUG("Destroying skybox.");
        m_skybox.reset();
    }
    else if (m_blend_mode == XR_ENVIRONMENT_BLEND_MODE_OPAQUE)
    {
        LOG_DEBUG("Allocating skybox.");
        m_skybox = OpenXrSkybox::Create(m_xr);
    }
}

auto OpenXrRuntime::PollEvents() -> Result<bool>
{
    while (true)
    {
        XrEventDataBuffer event{.type = XR_TYPE_EVENT_DATA_BUFFER};
        const auto result = xrPollEvent(m_instance->Handle(), &event);
        RETURN_IF_ERROR(m_instance->Check(result));
        if (result == XR_EVENT_UNAVAILABLE)
        {
            return true;
        }

        switch (event.type)
        {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
        {
            const auto& changed = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
            LOG_INFO("entered state {}", SessionStateName(changed.state));
            switch (changed.state)
            {
            case XR_SESSION_STATE_READY:
            {
                const XrSessionBeginInfo begin_info{.type = XR_TYPE_SESSION_BEGIN_INFO, .primaryViewConfigurationType = kViewType};
                RETURN_IF_ERROR(m_instance->Check(xrBeginSession(m_xr.session, &begin_info)));
                m_session_running = true;
                break;
            }
            case XR_SESSION_STATE_STOPPING:
                RETURN_IF_ERROR(m_instance->Check(xrEndSession(m_xr.session)));
                m_session_running = false;
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

auto OpenXrRuntime::UpdateFps() -> float
{
    const auto now = std::chrono::steady_clock::now();
    m_fps_counter.push_back(now);
    while (!m_fps_counter.empty() && std::chrono::duration<float>(now - m_fps_counter.front()).count() > 1.0f)
    {
        m_fps_counter.pop_front();
    }
    const float total_elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_fps_counter.front()).count();
    return static_cast<float>(m_fps_counter.size()) / total_elapsed;
}

auto OpenXrRuntime::Submit(std::span<const XrCompositionLayerBaseHeader* const> layers) -> Result<>
{
    const XrFrameEndInfo end_info{
        .type = XR_TYPE_FRAME_END_INFO,
        .displayTime = m_xr.predicted_display_time,
        .environmentBlendMode = m_blend_mode,
        .layerCount = static_cast<uint32_t>(layers.size()),
        .layers = layers.data(),
    };
    return m_instance->Check(xrEndFrame(m_xr.session, &end_info));
}

auto OpenXrRuntime::UpdateInput() -> Result<>
{
    g_interaction->PreUpdate();
    RETURN_IF_ERROR(m_input->Update(m_xr));
    g_interaction->PostUpdate();
    if (m_blocker)
    {
        m_blocker->Update(WatchId(), *m_monado);
    }
    if (m_playspace)
    {
        m_playspace->Update(*m_monado);
    }
    return UpdateIpd();
}

auto OpenXrRuntime::UpdateIpd() -> Result<>
{
    const XrViewLocateInfo locate_info{
        .type = XR_TYPE_VIEW_LOCATE_INFO,
        .viewConfigurationType = kViewType,
        .displayTime = m_xr.predicted_display_time,
        .space = m_xr.stage,
    };
    XrViewState view_state{.type = XR_TYPE_VIEW_STATE};
    std::array<XrView, 2> views{XrView{.type = XR_TYPE_VIEW}, XrView{.type = XR_TYPE_VIEW}};
    uint32_t count = 0;
    RETURN_IF_ERROR(m_instance->Check(
        xrLocateViews(m_xr.session, &locate_info, &view_state, static_cast<uint32_t>(views.size()), &count, views.data())
    ));

    if (g_interaction->DisplayPixelsPerRadian().minCoeff() <= 0.0f && count > 0)
    {
        g_interaction->SetDisplayPixelsPerRadian(DisplayDensity(*m_instance, views.front().fov));
        LOG_INFO(
            "Display density: {:.0f} x {:.0f} px/rad",
            g_interaction->DisplayPixelsPerRadian().x(),
            g_interaction->DisplayPixelsPerRadian().y()
        );
    }

    const float ipd = OpenXrPose::Ipd(std::span(views).first(count));
    if (std::abs(g_interaction->Ipd() - ipd) > 0.01f)
    {
        LOG_INFO("IPD changed: {} -> {}", g_interaction->Ipd(), ipd);
        g_interaction->SetIpd(ipd);
        Toast::Show(ToastMessage{.topic = ToastTopic::IpdChange, .title = "IPD", .body = std::format("{:.1f} mm", ipd)});
    }
    return {};
}

auto OpenXrRuntime::RenderOverlays() -> Result<>
{
    CommandBatch batch;

    if (!m_main_session_visible && m_skybox)
    {
        RETURN_IF_ERROR(m_skybox->Render(m_xr, batch));
    }

    for (auto& overlay : Views())
    {
        auto& data = m_overlay_data[overlay->Id()];
        data.SetCurVisible(false);
        if (!overlay->WantVisible())
        {
            continue;
        }

        if (!data.Init())
        {
            RETURN_IF_ERROR(overlay->Init());
            data.SetInit(true);
        }

        ASSIGN_OR_RETURN(const auto readiness, overlay->Composer()->ShouldRender());
        const bool should_render = readiness == RenderReadiness::Should
                                || (readiness == RenderReadiness::Can
                                    && std::abs(data.LastAlpha() - overlay->Alpha()) > std::numeric_limits<float>::epsilon());

        if (should_render)
        {
            ASSIGN_OR_RETURN(const bool ready, data.EnsureSwapchain(*overlay, m_xr));
            if (!ready)
            {
                continue;
            }
            ASSIGN_OR_RETURN(const auto target, data.Swapchain()->AcquireWaitImage());
            ASSIGN_OR_RETURN(const bool rendered, overlay->Composer()->Render(target, batch, overlay->Alpha()));
            if (!rendered)
            {
                RETURN_IF_ERROR(data.Swapchain()->EnsureImageReleased());
                continue;
            }
            data.SetLastAlpha(overlay->Alpha());
        }
        else if (!data.Swapchain())
        {
            continue;
        }
        data.SetCurVisible(true);
    }

    RETURN_IF_ERROR(m_lines->Render(batch));
    return batch.Submit();
}

auto OpenXrRuntime::ComposeLayers() -> Result<std::vector<std::pair<float, OpenXrLayer>>>
{
    std::vector<std::pair<float, OpenXrLayer>> layers;
    if (!m_main_session_visible && m_skybox)
    {
        ASSIGN_OR_RETURN(auto sky_layers, m_skybox->Present(m_xr));
        for (size_t i = 0; i < sky_layers.size(); ++i)
        {
            layers.emplace_back(std::fma(static_cast<float>(i), -50.0f, 200.0f), sky_layers[i]);
        }
    }

    for (auto& overlay : Views())
    {
        auto& data = m_overlay_data[overlay->Id()];
        if (!data.CurVisible())
        {
            continue;
        }
        const float dist_sq = (g_interaction->Hmd().translation() - overlay->Transform().translation()).squaredNorm()
                            + (100.0f - static_cast<float>(overlay->ZOrder()));
        if (!std::isnormal(dist_sq))
        {
            RETURN_IF_ERROR(data.Swapchain()->EnsureImageReleased());
            continue;
        }
        ASSIGN_OR_RETURN(auto layer, data.Present(*overlay, m_xr));
        if (layer)
        {
            layers.emplace_back(dist_sq, *layer);
        }
    }

    ASSIGN_OR_RETURN(auto line_layers, m_lines->Present(m_xr));
    for (auto& layer : line_layers)
    {
        layers.emplace_back(0.0f, layer);
    }
    return layers;
}

auto OpenXrRuntime::Release(std::unique_ptr<View> overlay) -> void
{
    OpenXrOverlay data;
    if (auto node = m_overlay_data.extract(overlay->Id()))
    {
        data = std::move(node.mapped());
    }
    m_delete_queue.push_back({.overlay = std::move(overlay), .data = std::move(data), .frame = Frame() + 5});
}

auto OpenXrRuntime::RunSystemTask(const SystemTask& task) -> void
{
    if (!m_playspace)
    {
        return;
    }
    if (std::holds_alternative<FixFloorTask>(task))
    {
        m_playspace->FixFloor(*m_monado);
    }
    else if (std::holds_alternative<ResetPlayspaceTask>(task))
    {
        m_playspace->ResetOffset(*m_monado);
    }
}

auto OpenXrRuntime::UpdateDevices() -> void
{
    if (m_monado)
    {
        OpenXrInput::UpdateDevices(*m_monado);
    }
}

auto OpenXrRuntime::ApplyHaptics(size_t pointer, const Haptics& haptics) -> void
{
    m_input->ApplyHaptics(m_xr, pointer, haptics);
}

auto OpenXrRuntime::DrawLine(Line line, const Eigen::Affine3f& pose, float length, size_t color) -> void
{
    m_lines->DrawFrom(m_line_ids.at(static_cast<size_t>(line)), pose, length, color, g_interaction->Hmd());
}

auto OpenXrRuntime::HideLine(Line) -> void
{
}

auto OpenXrRuntime::BeginFrame(bool running) -> Result<FrameStatus>
{
    if (!running)
    {
        LOG_WARN("Received shutdown signal.");
        const auto result = xrRequestExitSession(m_xr.session);
        if (result == XR_ERROR_SESSION_NOT_RUNNING)
        {
            return FrameStatus::Quit;
        }
        if (XR_FAILED(result))
        {
            LOG_ERROR("Failed to request OpenXR session exit: {}", m_instance->ResultName(result));
            return FrameStatus::Quit;
        }
        LOG_INFO("OpenXR session exit requested.");
    }

    ASSIGN_OR_RETURN(const bool keep_running, PollEvents());
    if (!keep_running)
    {
        return FrameStatus::Quit;
    }
    if (!m_session_running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return FrameStatus::Skip;
    }

    XrFrameState frame_state{.type = XR_TYPE_FRAME_STATE};
    const XrFrameWaitInfo wait_info{.type = XR_TYPE_FRAME_WAIT_INFO};
    RETURN_IF_ERROR(m_instance->Check(xrWaitFrame(m_xr.session, &wait_info, &frame_state)));
    const XrFrameBeginInfo begin_info{.type = XR_TYPE_FRAME_BEGIN_INFO};
    RETURN_IF_ERROR(m_instance->Check(xrBeginFrame(m_xr.session, &begin_info)));
    m_xr.predicted_display_time = frame_state.predictedDisplayTime;
    m_xr.fps = UpdateFps();

    if (frame_state.shouldRender == XR_FALSE)
    {
        RETURN_IF_ERROR(Submit({}));
        return FrameStatus::Skip;
    }
    return FrameStatus::Ready;
}

auto OpenXrRuntime::EndFrame() -> Result<>
{
    for (auto& overlay : Views())
    {
        RETURN_IF_ERROR(m_overlay_data[overlay->Id()].AfterInput(*overlay));
    }

    auto& watch = WatchOverlay();
    const Eigen::Affine3f watch_transform = watch.Transform();
    if (!watch.WantVisible())
    {
        watch.SetWantVisible(true);
        watch.SetTransform(Eigen::Affine3f(Eigen::Scaling(0.001f)));
    }

    RETURN_IF_ERROR(RenderOverlays());
    ASSIGN_OR_RETURN(auto layers, ComposeLayers());
    std::ranges::stable_sort(layers, [](const auto& a, const auto& b) -> bool { return a.first > b.first; });
    const auto headers = layers
                       | std::views::transform(
                             [](const auto& layer) -> const XrCompositionLayerBaseHeader* { return LayerHeader(layer.second); }
                       )
                       | std::ranges::to<std::vector>();
    RETURN_IF_ERROR(Submit(headers));

    std::erase_if(m_delete_queue, [this](const OpenXrPendingDelete& pending) -> bool { return pending.frame <= Frame(); });
    watch.SetTransform(watch_transform);
    return {};
}
