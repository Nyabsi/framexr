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

#pragma once

#include <memory>

#include <Eigen/Core>
#include <openvr.h>

#include "Device.hpp"
#include "Error.hpp"
#include "Image.hpp"
#include "Overlay.hpp"

struct OpenVrOverlayData {
    vr::VROverlayHandle_t handle = vr::k_ulOverlayHandleInvalid;
    bool visible = false;
    Eigen::Vector4f color = Eigen::Vector4f::Zero();
    float width = 0.0f;
    bool override_width = false;
    std::shared_ptr<Image> image;
    std::shared_ptr<Image> retired_image;
    bool image_dirty = false;
};

class OpenVrOverlay {
  public:
    static auto EnsureImageAllocated(Overlay& overlay, OpenVrOverlayData& data) -> Result<bool>;
    static auto AfterInput(Overlay& overlay, OpenVrOverlayData& data) -> Result<>;
    static auto AfterRender(vr::ETrackingUniverseOrigin universe, Overlay& overlay, OpenVrOverlayData& data) -> void;
    static auto Destroy(const Overlay& overlay, OpenVrOverlayData& data) -> void;

    static auto UploadColor(const Overlay& overlay, const OpenVrOverlayData& data) -> void;
    static auto UploadTransform(vr::ETrackingUniverseOrigin universe, Overlay& overlay, const OpenVrOverlayData& data) -> void;
    static auto UploadTexture(Overlay& overlay, OpenVrOverlayData& data) -> void;

  private:
    static auto Initialize(Overlay& overlay, OpenVrOverlayData& data) -> Result<>;
    static auto Show(Overlay& overlay, OpenVrOverlayData& data) -> Result<>;
    static auto Hide(Overlay& overlay, OpenVrOverlayData& data) -> Result<>;
    static auto UploadAlpha(const Overlay& overlay, const OpenVrOverlayData& data) -> void;
    static auto UploadWidth(const Overlay& overlay, const OpenVrOverlayData& data) -> void;
    static auto UploadCurvature(const Overlay& overlay, const OpenVrOverlayData& data) -> void;
    static auto UploadSortOrder(const Overlay& overlay, const OpenVrOverlayData& data) -> void;
};
