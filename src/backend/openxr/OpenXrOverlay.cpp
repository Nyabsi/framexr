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

#include "OpenXrOverlay.hpp"

#include <format>
#include <numbers>

#include "Logger.hpp"
#include "OpenXrPose.hpp"
#include "Overlay.hpp"

auto OpenXrOverlay::EnsureSwapchain(Overlay& overlay, const OpenXrState& xr) -> Result<bool> {
    const auto meta = overlay.Backend()->Meta();
    if (m_swapchain && meta) {
        const auto extent = m_swapchain->Extent();
        if (extent.width == meta->extent.width && extent.height == meta->extent.height) {
            return true;
        }
        RETURN_IF_ERROR(m_swapchain->EnsureImageReleased());
        m_swapchain.reset();
    }
    if (m_swapchain) {
        return true;
    }
    if (!meta) {
        LOG_WARN("{}: swapchain cannot be created due to missing metadata", overlay.State().Name());
        return false;
    }
    ASSIGN_OR_RETURN(m_swapchain, OpenXrSwapchain::Create(*xr.instance, xr.session, meta->extent, false));
    return true;
}

auto OpenXrOverlay::Present(Overlay& overlay, const OpenXrState& xr) -> Result<std::optional<OpenXrLayer>> {
    if (!m_swapchain) {
        LOG_WARN("{}: swapchain not ready", overlay.State().Name());
        return std::nullopt;
    }
    if (!m_swapchain->EverAcquired()) {
        LOG_WARN("{}: swapchain not rendered", overlay.State().Name());
        return std::nullopt;
    }
    RETURN_IF_ERROR(m_swapchain->EnsureImageReleased());

    const auto meta = overlay.Backend()->Meta();
    if (!meta) {
        FatalError(std::format("{}: frame meta missing after render", overlay.State().Name()));
    }
    const Eigen::Affine3f transform = overlay.State().Transform() * meta->transform;
    const Eigen::Matrix3f linear = transform.linear();

    const auto extent = m_swapchain->Extent();
    const float aspect_ratio = static_cast<float>(extent.height) / static_cast<float>(extent.width);
    float scale_x = 0.0f;
    float scale_y = 0.0f;
    if (aspect_ratio < 1.0f) {
        const float major = linear.col(0).norm();
        scale_x = major;
        scale_y = major * aspect_ratio;
    } else {
        const float major = linear.col(1).norm();
        scale_x = major / aspect_ratio;
        scale_y = major;
    }

    if (const auto curvature = overlay.State().Curvature()) {
        const float radius = scale_x / (2.0f * std::numbers::pi_v<float> * *curvature);
        const Eigen::Quaternionf rotation = OpenXrPose::NormalizedRotation(transform);
        const Eigen::Vector3f center = transform.translation() + rotation * (Eigen::Vector3f::UnitZ() * radius);
        return OpenXrLayer{XrCompositionLayerCylinderKHR{
            .type = XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR,
            .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
            .space = xr.stage,
            .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
            .subImage = m_swapchain->SubImage(),
            .pose = OpenXrPose::FromTranslationRotation(center, rotation),
            .radius = radius,
            .centralAngle = 2.0f * (scale_x / (2.0f * radius)),
            .aspectRatio = aspect_ratio,
        }};
    }

    return OpenXrLayer{XrCompositionLayerQuad{
        .type = XR_TYPE_COMPOSITION_LAYER_QUAD,
        .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
        .space = xr.stage,
        .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
        .subImage = m_swapchain->SubImage(),
        .pose = OpenXrPose::FromTransform(transform),
        .size = {.width = scale_x, .height = scale_y},
    }};
}

auto OpenXrOverlay::AfterInput(Overlay& overlay) -> Result<> {
    if (m_last_visible != overlay.State().WantVisible()) {
        if (overlay.State().WantVisible()) {
            RETURN_IF_ERROR(overlay.Backend()->Resume());
        } else {
            RETURN_IF_ERROR(overlay.Backend()->Pause());
        }
    }
    m_last_visible = overlay.State().WantVisible();
    return {};
}
