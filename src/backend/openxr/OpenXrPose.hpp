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

#include <span>

#include <Eigen/Geometry>

#include <openxr/openxr.h>

class OpenXrPose {
  public:
    static auto FromTransform(const Eigen::Affine3f& transform) -> XrPosef;
    static auto FromTranslationRotation(const Eigen::Vector3f& translation, const Eigen::Quaternionf& rotation) -> XrPosef;
    static auto ToTransform(const XrPosef& pose) -> Eigen::Affine3f;
    static auto ToQuaternion(const XrQuaternionf& orientation) -> Eigen::Quaternionf;
    static auto ToVector(const XrVector3f& position) -> Eigen::Vector3f;
    static auto NormalizedRotation(const Eigen::Affine3f& transform) -> Eigen::Quaternionf;
    static auto Lerp(const Eigen::Quaternionf& from, const Eigen::Quaternionf& to, float factor) -> Eigen::Quaternionf;
    static auto Ipd(std::span<const XrView> views) -> float;
};
