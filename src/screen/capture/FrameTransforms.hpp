// FrameXR
//
// This is a port of wlx-capture from Rust to C++.
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
// Copyright (C) 2023-2025 galister and wlx-capture contributors
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

#include <Eigen/Geometry>

#include "Frame.hpp"

class FrameTransforms final {
  public:
    static auto IsRotated90(FrameTransform transform) -> bool;
    static auto IsRotated180(FrameTransform transform) -> bool;
    static auto IsRotated270(FrameTransform transform) -> bool;
    static auto Affine(const Eigen::Vector2f& x_axis, const Eigen::Vector2f& y_axis, const Eigen::Vector2f& translation)
        -> Eigen::Affine2f;
};
