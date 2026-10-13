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

#include <cstddef>
#include <optional>
#include <string_view>

#include <Eigen/Geometry>

#include "Error.hpp"

class Monado;

struct OpenXrPlayspaceDrag {
    Eigen::Affine3f pose = Eigen::Affine3f::Identity();
    size_t hand = 0;
    Eigen::Vector3f hand_position = Eigen::Vector3f::Zero();
};

struct OpenXrPlayspaceRotate {
    Eigen::Affine3f pose = Eigen::Affine3f::Identity();
    size_t hand = 0;
    Eigen::Quaternionf hand_rotation = Eigen::Quaternionf::Identity();
};

class OpenXrPlayspace final {
  public:
    static auto Create(Monado& monado) -> Result<OpenXrPlayspace>;

    auto Update(Monado& monado) -> void;
    auto ResetOffset(Monado& monado) -> void;
    auto FixFloor(Monado& monado) -> void;

  private:
    explicit OpenXrPlayspace(const Eigen::Affine3f& last_transform);

    auto UpdateRotate(Monado& monado) -> bool;
    auto UpdateDrag(Monado& monado) -> void;
    auto InterruptMoves(std::string_view reason) -> void;

    static auto ApplyOffset(const Eigen::Affine3f& transform, Monado& monado) -> void;

    Eigen::Affine3f m_last_transform;
    std::optional<OpenXrPlayspaceDrag> m_drag;
    std::optional<OpenXrPlayspaceRotate> m_rotate;
};
