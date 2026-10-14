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

#include <Eigen/Geometry>
#include <openvr.h>

struct OpenVrPlayspaceDrag
{
    Eigen::Affine3f pose = Eigen::Affine3f::Identity();
    size_t hand = 0;
    Eigen::Vector3f hand_position = Eigen::Vector3f::Zero();
};

struct OpenVrPlayspaceRotate
{
    Eigen::Affine3f pose = Eigen::Affine3f::Identity();
    size_t hand = 0;
    Eigen::Quaternionf hand_rotation = Eigen::Quaternionf::Identity();
};

class OpenVrPlayspace final
{
  public:
    auto Update() -> void;
    auto ResetOffset() -> void;
    auto FixFloor() -> void;
    auto PlayspaceChanged() -> void;

    [[nodiscard]] auto Universe() const -> vr::ETrackingUniverseOrigin { return m_universe; }

  private:
    auto UpdateRotate() -> bool;
    auto UpdateDrag() -> void;
    [[nodiscard]] auto WorkingCopy() const -> std::optional<Eigen::Affine3f>;
    auto SetWorkingCopy(const Eigen::Affine3f& pose) const -> void;
    auto InterruptMoves(std::string_view reason) -> void;

    static auto UniverseName(vr::ETrackingUniverseOrigin universe) -> std::string_view;
    static auto ApplyChaperoneOffset(const Eigen::Vector3f& offset) -> void;
    static auto ApplyChaperoneTransform(const Eigen::Affine3f& transform) -> void;

    vr::ETrackingUniverseOrigin m_universe = vr::TrackingUniverseRawAndUncalibrated;
    std::optional<OpenVrPlayspaceDrag> m_drag;
    std::optional<OpenVrPlayspaceRotate> m_rotate;
};
