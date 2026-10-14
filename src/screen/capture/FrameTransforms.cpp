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

#include "FrameTransforms.hpp"

auto FrameTransforms::Affine(const Eigen::Vector2f& x_axis, const Eigen::Vector2f& y_axis, const Eigen::Vector2f& translation)
    -> Eigen::Affine2f {
    Eigen::Affine2f result = Eigen::Affine2f::Identity();
    result.linear().col(0) = x_axis;
    result.linear().col(1) = y_axis;
    result.translation() = translation;
    return result;
}

auto FrameTransforms::IsRotated90(FrameTransform transform) -> bool {
    return transform == FrameTransform::Rotated90 || transform == FrameTransform::Flipped90;
}

auto FrameTransforms::IsRotated180(FrameTransform transform) -> bool {
    return transform == FrameTransform::Rotated180 || transform == FrameTransform::Flipped180;
}

auto FrameTransforms::IsRotated270(FrameTransform transform) -> bool {
    return transform == FrameTransform::Rotated270 || transform == FrameTransform::Flipped270;
}
