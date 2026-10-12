// FrameXR
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
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
#include <optional>
#include <span>

#include <Eigen/Geometry>

#include "Overlay.hpp"
#include "OverlayRenderer.hpp"

struct ViewRect {
    float min_azimuth = 0.0f;
    float max_azimuth = 0.0f;
    float min_elevation = 0.0f;
    float max_elevation = 0.0f;
};

class OverlayLayout final {
  public:
    static auto Corners(Overlay& overlay) -> std::optional<std::array<Eigen::Vector3f, 4>>;
    static auto ViewBounds(const std::array<Eigen::Vector3f, 4>& corners, const Eigen::Affine3f& head) -> ViewRect;
    static auto Overlaps(const ViewRect& a, const ViewRect& b, float gap) -> bool;
    static auto MeasureFootprint(Overlay& overlay, const Eigen::Affine3f& head, const Eigen::Vector2f& display_pixels_per_radian)
        -> std::optional<TexelFootprint>;
    static auto NudgeClear(Overlay& moving, std::span<Overlay* const> fixed, const Eigen::Affine3f& head) -> bool;

  private:
    static auto RotateAroundHead(Overlay& overlay, const Eigen::Affine3f& head, const Eigen::Vector3f& axis, float angle) -> void;
};
