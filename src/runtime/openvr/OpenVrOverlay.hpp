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
#include "View.hpp"

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
    static auto EnsureImageAllocated(View& overlay, OpenVrOverlayData& data) -> Result<bool>;
    static auto AfterInput(View& overlay, OpenVrOverlayData& data) -> Result<>;
    static auto AfterRender(vr::ETrackingUniverseOrigin universe, View& overlay, OpenVrOverlayData& data) -> void;
    static auto Destroy(const View& overlay, OpenVrOverlayData& data) -> void;

    static auto UploadColor(const View& overlay, const OpenVrOverlayData& data) -> void;
    static auto UploadTransform(vr::ETrackingUniverseOrigin universe, View& overlay, const OpenVrOverlayData& data) -> void;
    static auto UploadTexture(View& overlay, OpenVrOverlayData& data) -> void;

  private:
    static auto Initialize(View& overlay, OpenVrOverlayData& data) -> Result<>;
    static auto Show(View& overlay, OpenVrOverlayData& data) -> Result<>;
    static auto Hide(View& overlay, OpenVrOverlayData& data) -> Result<>;
    static auto UploadAlpha(const View& overlay, const OpenVrOverlayData& data) -> void;
    static auto UploadWidth(const View& overlay, const OpenVrOverlayData& data) -> void;
    static auto UploadCurvature(const View& overlay, const OpenVrOverlayData& data) -> void;
    static auto UploadSortOrder(const View& overlay, const OpenVrOverlayData& data) -> void;
};
