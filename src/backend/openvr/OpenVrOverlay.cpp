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

#include "OpenVrOverlay.hpp"

#include <cmath>
#include <format>
#include <limits>

#include "Logger.hpp"
#include "OpenVrMath.hpp"
#include "State.hpp"

namespace {

auto ErrorName(vr::EVROverlayError error) -> const char* {
    return vr::VROverlay()->GetOverlayErrorNameFromEnum(error);
}

} // namespace

auto OpenVrOverlay::Initialize(AppState& app, Overlay& overlay, OpenVrOverlayData& data) -> Result<> {
    const auto key = std::format("framexr-{}", overlay.state.name);
    LOG_DEBUG("Create overlay with key: {}", key);
    if (const auto error = vr::VROverlay()->CreateOverlay(key.c_str(), key.c_str(), &data.handle);
        error != vr::VROverlayError_None) {
        FatalError(std::format("Failed to create overlay: {}", ErrorName(error)));
    }
    LOG_DEBUG("{}: initialize", overlay.state.name);

    data.color = Eigen::Vector4f::Ones();
    RETURN_IF_ERROR(overlay.Init(app));

    if (data.width < std::numeric_limits<float>::epsilon()) {
        data.width = 1.0f;
    }

    UploadWidth(overlay, data);
    UploadColor(overlay, data);
    UploadAlpha(overlay, data);
    UploadCurvature(overlay, data);
    UploadSortOrder(overlay, data);
    return {};
}

auto OpenVrOverlay::EnsureImageAllocated(AppState& app, Overlay& overlay, OpenVrOverlayData& data) -> Result<bool> {
    const auto meta = overlay.backend->Meta();
    if (data.image && meta) {
        const auto extent = data.image->Extent();
        if (extent.width == meta->extent.width && extent.height == meta->extent.height) {
            return true;
        }
        data.retired_image = std::move(data.image);
    }
    if (data.image) {
        return true;
    }
    if (!meta) {
        return false;
    }
    ASSIGN_OR_RETURN(
        data.image,
        app.device->CreateRenderTexture(
            meta->extent.width,
            meta->extent.height,
            app.device->NativeFormat(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        )
    );
    return true;
}

auto OpenVrOverlay::AfterInput(AppState& app, Overlay& overlay, OpenVrOverlayData& data) -> Result<> {
    if (overlay.state.want_visible && !data.visible) {
        return Show(app, overlay, data);
    }
    if (!overlay.state.want_visible && data.visible) {
        return Hide(app, overlay, data);
    }
    return {};
}

auto OpenVrOverlay::AfterRender(vr::ETrackingUniverseOrigin universe, Overlay& overlay, OpenVrOverlayData& data, Device& device)
    -> void {
    if (!data.visible) {
        return;
    }
    if (overlay.state.dirty) {
        UploadCurvature(overlay, data);
        UploadTransform(universe, overlay, data);
        UploadAlpha(overlay, data);
        overlay.state.dirty = false;
    }
    UploadTexture(overlay, data, device);
}

auto OpenVrOverlay::Show(AppState& app, Overlay& overlay, OpenVrOverlayData& data) -> Result<> {
    if (data.handle == vr::k_ulOverlayHandleInvalid) {
        RETURN_IF_ERROR(Initialize(app, overlay, data));
    }
    LOG_DEBUG("{}: show", overlay.state.name);
    if (const auto error = vr::VROverlay()->ShowOverlay(data.handle); error != vr::VROverlayError_None) {
        LOG_ERROR("{}: Failed to show overlay: {}", overlay.state.name, ErrorName(error));
    }
    data.visible = true;
    return overlay.backend->Resume(app);
}

auto OpenVrOverlay::Hide(AppState& app, Overlay& overlay, OpenVrOverlayData& data) -> Result<> {
    if (data.handle == vr::k_ulOverlayHandleInvalid) {
        return {};
    }
    LOG_DEBUG("{}: hide", overlay.state.name);
    if (const auto error = vr::VROverlay()->HideOverlay(data.handle); error != vr::VROverlayError_None) {
        LOG_ERROR("{}: Failed to hide overlay: {}", overlay.state.name, ErrorName(error));
    }
    data.visible = false;
    return overlay.backend->Pause(app);
}

auto OpenVrOverlay::UploadAlpha(const Overlay& overlay, const OpenVrOverlayData& data) -> void {
    if (data.handle == vr::k_ulOverlayHandleInvalid) {
        LOG_DEBUG("{}: No overlay handle", overlay.state.name);
        return;
    }
    if (const auto error = vr::VROverlay()->SetOverlayAlpha(data.handle, overlay.state.alpha); error != vr::VROverlayError_None) {
        LOG_ERROR("{}: Failed to set overlay alpha: {}", overlay.state.name, ErrorName(error));
    }
}

auto OpenVrOverlay::UploadColor(const Overlay& overlay, const OpenVrOverlayData& data) -> void {
    if (data.handle == vr::k_ulOverlayHandleInvalid) {
        LOG_DEBUG("{}: No overlay handle", overlay.state.name);
        return;
    }
    vr::VROverlay()->SetOverlayColor(data.handle, data.color.x(), data.color.y(), data.color.z());
    if (const auto error = vr::VROverlay()->SetOverlayAlpha(data.handle, data.color.w()); error != vr::VROverlayError_None) {
        LOG_ERROR("{}: Failed to set overlay tint: {}", overlay.state.name, ErrorName(error));
    }
}

auto OpenVrOverlay::UploadWidth(const Overlay& overlay, const OpenVrOverlayData& data) -> void {
    if (data.handle == vr::k_ulOverlayHandleInvalid) {
        LOG_DEBUG("{}: No overlay handle", overlay.state.name);
        return;
    }
    if (const auto error = vr::VROverlay()->SetOverlayWidthInMeters(data.handle, data.width); error != vr::VROverlayError_None) {
        LOG_ERROR("{}: Failed to set overlay width: {}", overlay.state.name, ErrorName(error));
    }
}

auto OpenVrOverlay::UploadCurvature(const Overlay& overlay, const OpenVrOverlayData& data) -> void {
    if (data.handle == vr::k_ulOverlayHandleInvalid) {
        LOG_DEBUG("{}: No overlay handle", overlay.state.name);
        return;
    }
    const auto curvature = overlay.state.curvature.value_or(0.0f);
    if (const auto error = vr::VROverlay()->SetOverlayCurvature(data.handle, curvature); error != vr::VROverlayError_None) {
        LOG_ERROR("{}: Failed to set overlay curvature: {}", overlay.state.name, ErrorName(error));
    }
}

auto OpenVrOverlay::UploadSortOrder(const Overlay& overlay, const OpenVrOverlayData& data) -> void {
    if (data.handle == vr::k_ulOverlayHandleInvalid) {
        LOG_DEBUG("{}: No overlay handle", overlay.state.name);
        return;
    }
    if (const auto error = vr::VROverlay()->SetOverlaySortOrder(data.handle, overlay.state.z_order);
        error != vr::VROverlayError_None) {
        LOG_ERROR("{}: Failed to set overlay z order: {}", overlay.state.name, ErrorName(error));
    }
}

auto OpenVrOverlay::UploadTransform(vr::ETrackingUniverseOrigin universe, Overlay& overlay, const OpenVrOverlayData& data) -> void {
    if (data.handle == vr::k_ulOverlayHandleInvalid) {
        LOG_DEBUG("{}: No overlay handle", overlay.state.name);
        return;
    }
    const auto meta = overlay.backend->Meta();
    const Eigen::Affine3f effective = overlay.state.transform * (meta ? meta->transform : Eigen::Affine3f::Identity());
    const auto matrix = OpenVrMath::ToHmdMatrix(effective);
    if (const auto error = vr::VROverlay()->SetOverlayTransformAbsolute(data.handle, universe, &matrix);
        error != vr::VROverlayError_None) {
        LOG_ERROR("{}: Failed to set overlay transform: {}", overlay.state.name, ErrorName(error));
    }
}

auto OpenVrOverlay::UploadTexture(Overlay& overlay, OpenVrOverlayData& data, Device& device) -> void {
    if (data.handle == vr::k_ulOverlayHandleInvalid) {
        LOG_DEBUG("{}: No overlay handle", overlay.state.name);
        return;
    }
    if (!data.image) {
        LOG_DEBUG("{}: Not rendered", overlay.state.name);
        return;
    }
    if (!data.image_dirty) {
        return;
    }
    data.image_dirty = false;

    const auto extent = data.image->Extent();
    if (!data.override_width) {
        const float width = std::min(static_cast<float>(extent.width) / static_cast<float>(extent.height), 1.0f);
        if (std::abs(width - data.width) > std::numeric_limits<float>::epsilon()) {
            LOG_INFO("{}: New width {}", overlay.state.name, width);
            data.width = width;
            UploadWidth(overlay, data);
        }
    }

    vr::VRVulkanTextureData_t vulkan_texture{
        .m_nImage = reinterpret_cast<uint64_t>(data.image->Handle()),
        .m_pDevice = device.Handle(),
        .m_pPhysicalDevice = device.PhysicalDevice(),
        .m_pInstance = device.Instance(),
        .m_pQueue = device.GraphicsQueue().Handle(),
        .m_nQueueFamilyIndex = device.GraphicsQueue().Family(),
        .m_nWidth = extent.width,
        .m_nHeight = extent.height,
        .m_nFormat = static_cast<uint32_t>(data.image->Format()),
        .m_nSampleCount = 1,
    };
    LOG_TRACE("{}: UploadTex {}, {}x{}", overlay.state.name, static_cast<int>(data.image->Format()), extent.width, extent.height);

    vr::Texture_t texture{
        .handle = &vulkan_texture,
        .eType = vr::TextureType_Vulkan,
        .eColorSpace = vr::ColorSpace_Auto,
    };
    if (const auto error = vr::VROverlay()->SetOverlayTexture(data.handle, &texture); error != vr::VROverlayError_None) {
        LOG_ERROR("{}: Failed to set overlay texture: {}", overlay.state.name, ErrorName(error));
    }
}

auto OpenVrOverlay::Destroy(const Overlay& overlay, OpenVrOverlayData& data) -> void {
    if (data.handle == vr::k_ulOverlayHandleInvalid) {
        return;
    }
    LOG_DEBUG("{}: destroy", overlay.state.name);
    if (const auto error = vr::VROverlay()->DestroyOverlay(data.handle); error != vr::VROverlayError_None) {
        LOG_ERROR("{}: Failed to destroy overlay: {}", overlay.state.name, ErrorName(error));
    }
    data.handle = vr::k_ulOverlayHandleInvalid;
}
