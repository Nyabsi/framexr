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

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include <Eigen/Geometry>
#include <openvr.h>

#include "Device.hpp"
#include "Error.hpp"
#include "Image.hpp"
#include "OpenVrOverlay.hpp"
#include "Overlay.hpp"

class AppState;

class OpenVrStaticRenderer final : public OverlayRenderer {
  public:
    explicit OpenVrStaticRenderer(std::shared_ptr<Image> image);

    auto Init(AppState& app) -> Result<> override;
    auto Pause(AppState& app) -> Result<> override;
    auto Resume(AppState& app) -> Result<> override;
    auto ShouldRender(AppState& app) -> Result<RenderReadiness> override;
    auto Render(AppState& app, const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> override;
    auto Meta() -> std::optional<FrameMeta> override;

  private:
    std::shared_ptr<Image> image_;
};

struct OpenVrLine {
    size_t id = 0;
    std::unique_ptr<Overlay> overlay;
    OpenVrOverlayData data;
};

class OpenVrLines final {
  public:
    static auto Create(Device& device) -> Result<OpenVrLines>;

    auto Allocate() -> size_t;
    auto DrawFrom(size_t id, Eigen::Affine3f from, float length, size_t color, const Eigen::Affine3f& hmd) -> void;
    auto Hide(size_t id) -> void;
    auto Update(vr::ETrackingUniverseOrigin universe, AppState& app) -> Result<>;
    auto MarkDirty() -> void;

  private:
    explicit OpenVrLines(std::shared_ptr<Image> image);

    std::shared_ptr<Image> image_;
    std::vector<OpenVrLine> lines_;
    std::array<Eigen::Vector4f, 5> colors_;
};
