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

#include "OpenVrRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

#include <openvr.h>

#include "Interaction.hpp"
#include "Logger.hpp"
#include "OpenVrManifest.hpp"
#include "Compose.hpp"
#include "Toast.hpp"

namespace
{

constexpr const char* kSteamVrSection = "steamvr";

constexpr std::array<const char*, 3> kColorGainKeys{"hmdDisplayColorGainR", "hmdDisplayColorGainG", "hmdDisplayColorGainB"};

auto SplitExtensions(const std::string& text) -> std::vector<std::string>
{
    std::vector<std::string> extensions;
    std::istringstream stream(text);
    for (std::string name; stream >> name;)
    {
        extensions.push_back(name);
    }
    return extensions;
}

auto InstanceExtensions() -> std::vector<std::string>
{
    auto* compositor = vr::VRCompositor();
    const auto length = compositor->GetVulkanInstanceExtensionsRequired(nullptr, 0);
    std::string buffer(length, '\0');
    if (length > 0)
    {
        compositor->GetVulkanInstanceExtensionsRequired(buffer.data(), length);
    }
    return SplitExtensions(buffer.c_str());
}

auto DeviceExtensions(VkPhysicalDevice physical_device) -> std::vector<std::string>
{
    auto* compositor = vr::VRCompositor();
    const auto length = compositor->GetVulkanDeviceExtensionsRequired(physical_device, nullptr, 0);
    std::string buffer(length, '\0');
    if (length > 0)
    {
        compositor->GetVulkanDeviceExtensionsRequired(physical_device, buffer.data(), length);
    }
    return SplitExtensions(buffer.c_str());
}

} // namespace

auto OpenVrRuntime::ReadIpd() -> std::optional<float>
{
    vr::ETrackedPropertyError error = vr::TrackedProp_Success;
    const float ipd = vr::VRSystem()
                          ->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_UserIpdMeters_Float, &error);
    if (error != vr::TrackedProp_Success)
    {
        return std::nullopt;
    }
    return std::round(ipd * 10000.0f) * 0.1f;
}

auto OpenVrRuntime::ReadDisplayDensity() -> Eigen::Vector2f
{
    uint32_t width = 0;
    uint32_t height = 0;
    vr::VRSystem()->GetRecommendedRenderTargetSize(&width, &height);
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
    vr::VRSystem()->GetProjectionRaw(vr::Eye_Left, &left, &right, &top, &bottom);
    if (right - left <= 0.0f || std::abs(bottom - top) <= 0.0f)
    {
        return Eigen::Vector2f::Zero();
    }
    return {static_cast<float>(width) / (right - left), static_cast<float>(height) / std::abs(bottom - top)};
}

auto OpenVrRuntime::Connect() -> std::unique_ptr<OpenVrRuntime>
{
    vr::EVRInitError error = vr::VRInitError_None;
    vr::VR_Init(&error, vr::VRApplication_Overlay);
    if (error != vr::VRInitError_None)
    {
        LOG_WARN("Will not use OpenVR: Context init failed");
        return nullptr;
    }
    LOG_INFO("Using OpenVR runtime");
    return std::unique_ptr<OpenVrRuntime>(new OpenVrRuntime());
}

auto OpenVrRuntime::Uninstall() -> void
{
    if (const auto runtime = Connect(); runtime == nullptr)
    {
        LOG_ERROR("Uninstall failed: could not reach OpenVR");
    }
    else if (auto result = OpenVrManifest::Uninstall(); !result)
    {
        LOG_DEBUG("{}", result.error().Message());
    }
}

OpenVrRuntime::~OpenVrRuntime()
{
    m_lines.reset();
    m_overlay_data.clear();
    Stop();
    vr::VR_Shutdown();
    LOG_WARN("OpenVR shutdown");
}

auto OpenVrRuntime::Open() -> Result<>
{
    DeviceCreateInfo device_info;
    device_info.instance_extensions = InstanceExtensions();
    device_info.required_device_extensions = [](VkPhysicalDevice physical_device) -> std::vector<std::string>
    { return DeviceExtensions(physical_device); };
    ASSIGN_OR_RETURN(m_device, Device::Create(device_info));
    RETURN_IF_ERROR(Start());

    if (const auto ipd = ReadIpd())
    {
        g_interaction->SetIpd(*ipd);
        LOG_INFO("IPD: {:.1f} mm", *ipd);
    }
    g_interaction->SetDisplayPixelsPerRadian(ReadDisplayDensity());
    LOG_INFO(
        "Display density: {:.0f} x {:.0f} px/rad",
        g_interaction->DisplayPixelsPerRadian().x(),
        g_interaction->DisplayPixelsPerRadian().y()
    );

    if (auto installed = OpenVrManifest::Install(); !installed)
    {
        LOG_DEBUG("{}", installed.error().Message());
    }

    RETURN_IF_ERROR(OpenVrInput::SetActionManifest());
    ASSIGN_OR_RETURN(m_input, OpenVrInput::Create());

    vr::ETrackedPropertyError error = vr::TrackedProp_Success;
    const float refresh_rate = vr::VRSystem()->GetFloatTrackedDeviceProperty(
        vr::k_unTrackedDeviceIndex_Hmd,
        vr::Prop_DisplayFrequency_Float,
        &error
    );
    if (error != vr::TrackedProp_Success)
    {
        return Unexpected("Failed to get HMD refresh rate");
    }
    LOG_INFO("HMD running @ {} Hz", refresh_rate);
    m_frame_timeout = 2 * static_cast<uint32_t>(std::floor(1000.0f / refresh_rate));

    ASSIGN_OR_RETURN(m_lines, OpenVrLines::Create());
    for (auto& id : m_line_ids)
    {
        id = m_lines->Allocate();
    }
    m_playspace.PlayspaceChanged();
    return {};
}

auto OpenVrRuntime::Release(std::unique_ptr<View> overlay) -> void
{
    if (const auto it = m_overlay_data.find(overlay->Id()); it != m_overlay_data.end())
    {
        OpenVrOverlay::Destroy(*overlay, it->second);
        m_overlay_data.erase(it);
    }
}

auto OpenVrRuntime::AdjustGain(ColorChannel channel, float delta) -> void
{
    auto* settings = vr::VRSettings();
    std::array<float, 3> current{};
    for (size_t i = 0; i < current.size(); ++i)
    {
        vr::EVRSettingsError error = vr::VRSettingsError_None;
        current[i] = settings->GetFloat(kSteamVrSection, kColorGainKeys[i], &error);
        if (error != vr::VRSettingsError_None)
        {
            return;
        }
    }

    float min = current[0] + current[1] + current[2] < 0.11f ? 0.1f : 0.0f;
    const auto set = [&](size_t index) -> bool
    {
        vr::EVRSettingsError error = vr::VRSettingsError_None;
        settings->SetFloat(kSteamVrSection, kColorGainKeys[index], std::clamp(current[index] + delta, min, 1.0f), &error);
        return error == vr::VRSettingsError_None;
    };

    switch (channel)
    {
    case ColorChannel::R: set(0); break;
    case ColorChannel::G: set(1); break;
    case ColorChannel::B: set(2); break;
    case ColorChannel::All:
        min *= 0.3333f;
        if (set(0) && set(1))
        {
            set(2);
        }
        break;
    }
}

auto OpenVrRuntime::PollEvents() -> bool
{
    vr::VREvent_t event{};
    while (vr::VRSystem()->PollNextEvent(&event, sizeof(event)))
    {
        switch (event.eventType)
        {
        case vr::VREvent_Quit: LOG_WARN("Received quit event, shutting down."); return false;
        case vr::VREvent_TrackedDeviceActivated:
        case vr::VREvent_TrackedDeviceDeactivated:
        case vr::VREvent_TrackedDeviceUpdated: m_input->UpdateDevices(); break;
        case vr::VREvent_SeatedZeroPoseReset:
        case vr::VREvent_StandingZeroPoseReset:
        case vr::VREvent_ChaperoneUniverseHasChanged:
        case vr::VREvent_SceneApplicationChanged: m_playspace.PlayspaceChanged(); break;
        case vr::VREvent_IpdChanged:
            if (const auto ipd = ReadIpd())
            {
                if (std::abs(*ipd - g_interaction->Ipd()) > 0.05f)
                {
                    LOG_INFO("IPD: {:.1f} mm -> {:.1f} mm", g_interaction->Ipd(), *ipd);
                    Toast::Show(
                        ToastMessage{.topic = ToastTopic::IpdChange, .title = "IPD", .body = std::format("{:.1f} mm", *ipd)}
                    );
                }
                g_interaction->SetIpd(*ipd);
            }
            break;
        default: break;
        }
    }
    return true;
}

auto OpenVrRuntime::RunSystemTask(const SystemTask& task) -> void
{
    if (const auto* gain = std::get_if<ColorGainTask>(&task))
    {
        AdjustGain(gain->channel, gain->delta);
    }
    else if (std::holds_alternative<FixFloorTask>(task))
    {
        m_playspace.FixFloor();
    }
    else if (std::holds_alternative<ResetPlayspaceTask>(task))
    {
        m_playspace.ResetOffset();
    }
    else if (std::holds_alternative<ShowHideTask>(task))
    {
        m_lines->MarkDirty();
    }
}

auto OpenVrRuntime::BeginFrame(bool running) -> Result<FrameStatus>
{
    vr::VROverlay()->WaitFrameSync(m_frame_timeout);
    if (!running)
    {
        LOG_WARN("Received shutdown signal.");
        return FrameStatus::Quit;
    }
    return PollEvents() ? FrameStatus::Ready : FrameStatus::Quit;
}

auto OpenVrRuntime::UpdateDevices() -> void
{
    m_input->UpdateDevices();
}

auto OpenVrRuntime::UpdateInput() -> Result<>
{
    g_interaction->PreUpdate();
    m_input->Update(m_playspace.Universe());
    g_interaction->PostUpdate();
    m_playspace.Update();
    return {};
}

auto OpenVrRuntime::ApplyHaptics(size_t pointer, const Haptics& haptics) -> void
{
    m_input->TriggerHaptics(pointer, haptics);
}

auto OpenVrRuntime::DrawLine(Line line, const Eigen::Affine3f& pose, float length, size_t color) -> void
{
    m_lines->DrawFrom(m_line_ids.at(static_cast<size_t>(line)), pose, length, color, g_interaction->Hmd());
}

auto OpenVrRuntime::HideLine(Line line) -> void
{
    m_lines->Hide(m_line_ids.at(static_cast<size_t>(line)));
}

auto OpenVrRuntime::EndFrame() -> Result<>
{
    const auto universe = m_playspace.Universe();
    RETURN_IF_ERROR(m_lines->Update(universe));
    for (auto& overlay : Views())
    {
        RETURN_IF_ERROR(OpenVrOverlay::AfterInput(*overlay, m_overlay_data[overlay->Id()]));
    }

    CommandBatch batch;
    for (auto& overlay : Views())
    {
        if (!overlay->WantVisible())
        {
            continue;
        }
        ASSIGN_OR_RETURN(const auto readiness, overlay->Composer()->ShouldRender());
        if (readiness != RenderReadiness::Should)
        {
            continue;
        }
        auto& data = m_overlay_data[overlay->Id()];
        ASSIGN_OR_RETURN(const auto allocated, OpenVrOverlay::EnsureImageAllocated(*overlay, data));
        if (allocated)
        {
            ASSIGN_OR_RETURN(data.image_dirty, overlay->Composer()->Render(data.image, batch, 1.0f));
        }
    }
    RETURN_IF_ERROR(batch.Submit());

    for (auto& overlay : Views())
    {
        OpenVrOverlay::AfterRender(universe, *overlay, m_overlay_data[overlay->Id()]);
    }
    return {};
}
