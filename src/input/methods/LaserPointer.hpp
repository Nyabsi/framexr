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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <Eigen/Geometry>

#include "OneEuroFilter.hpp"
#include "PoseFilter.hpp"

class View;

enum class OverlayId : uint64_t {};

enum class PointerMode : uint8_t {
    Left,
    Right,
    Middle,
    Special,
};

struct Haptics {
    float intensity = 0.0f;
    float duration = 0.0f;
    float frequency = 0.0f;
};

struct PointerHit {
    size_t pointer = 0;
    OverlayId overlay{};
    PointerMode mode = PointerMode::Left;
    bool primary = false;
    Eigen::Vector2f uv = Eigen::Vector2f::Zero();
    float dist = 0.0f;
};

struct PointerState {
    float scroll_x = 0.0f;
    float scroll_y = 0.0f;
    bool click = false;
    bool grab = false;
    bool alt_click = false;
    bool space_drag = false;
    bool space_rotate = false;
    bool space_reset = false;
    bool click_modifier_right = false;
    bool click_modifier_middle = false;
    bool move_mouse = false;
};

struct GrabData {
    Eigen::Affine3f offset = Eigen::Affine3f::Identity();
    OverlayId grabbed_id{};
    Eigen::Vector3f grab_point = Eigen::Vector3f::Zero();
    std::optional<float> old_curvature;
    bool grab_all = false;
    std::optional<float> bend_from;
    float bend_curvature = 0.0f;
};

struct RayHit {
    OverlayId overlay{};
    Eigen::Vector3f global_position = Eigen::Vector3f::Zero();
    Eigen::Vector2f local_position = Eigen::Vector2f::Zero();
    float dist = 0.0f;
};

struct InteractionState {
    PointerMode mode = PointerMode::Left;
    std::optional<GrabData> grabbed;
    std::optional<OverlayId> clicked_id;
    std::optional<OverlayId> hovered_id;
    std::optional<float> haptics;
};

struct InteractionResult {
    float length = 0.0f;
    std::optional<Haptics> haptics;
    std::optional<Eigen::Vector3f> target;
};

class LaserPointer {
  public:
    explicit LaserPointer(size_t index);

    [[nodiscard]] auto Index() const -> size_t { return m_index; }
    [[nodiscard]] auto Pose() const -> const Eigen::Affine3f& { return m_pose; }
    [[nodiscard]] auto Pose() -> Eigen::Affine3f& { return m_pose; }
    [[nodiscard]] auto RawPose() const -> const Eigen::Affine3f& { return m_raw_pose; }
    [[nodiscard]] auto RawPose() -> Eigen::Affine3f& { return m_raw_pose; }
    [[nodiscard]] auto Now() const -> const PointerState& { return m_now; }
    [[nodiscard]] auto Now() -> PointerState& { return m_now; }
    [[nodiscard]] auto Before() const -> const PointerState& { return m_before; }
    [[nodiscard]] auto Before() -> PointerState& { return m_before; }
    [[nodiscard]] auto LastClick() const -> const std::chrono::steady_clock::time_point& { return m_last_click; }
    [[nodiscard]] auto LastClick() -> std::chrono::steady_clock::time_point& { return m_last_click; }
    [[nodiscard]] auto TrackedPose() const -> const Eigen::Affine3f& { return m_tracked_pose; }
    [[nodiscard]] auto TrackedPose() -> Eigen::Affine3f& { return m_tracked_pose; }
    [[nodiscard]] auto AimFilter() const -> const PoseFilter& { return m_aim_filter; }
    [[nodiscard]] auto AimFilter() -> PoseFilter& { return m_aim_filter; }
    [[nodiscard]] auto Interaction() const -> const InteractionState& { return m_interaction; }
    [[nodiscard]] auto Interaction() -> InteractionState& { return m_interaction; }

    auto SetPose(Eigen::Affine3f pose) -> void { m_pose = std::move(pose); }
    auto SetRawPose(Eigen::Affine3f raw_pose) -> void { m_raw_pose = std::move(raw_pose); }
    auto SetNow(PointerState now) -> void { m_now = now; }
    auto SetBefore(PointerState before) -> void { m_before = before; }
    auto SetLastClick(std::chrono::steady_clock::time_point last_click) -> void { m_last_click = last_click; }
    auto SetTrackedPose(Eigen::Affine3f tracked_pose) -> void { m_tracked_pose = std::move(tracked_pose); }
    auto SetInteraction(InteractionState interaction) -> void { m_interaction = std::move(interaction); }

    auto Interact() -> InteractionResult;

    static auto LinePose(const Eigen::Affine3f& pose, const std::optional<Eigen::Vector3f>& target) -> Eigen::Affine3f;

  private:
    [[nodiscard]] auto NearestHit() const -> std::optional<PointerHit>;
    [[nodiscard]] auto RayTest(OverlayId overlay, const Eigen::Affine3f& transform, std::optional<float> curvature) const
        -> std::optional<RayHit>;
    auto StartGrab(View& overlay, const Eigen::Vector3f& point) -> void;
    auto HandleGrabbed(View& overlay) -> void;
    auto ReleaseClick(const std::optional<PointerHit>& hit) -> void;

    size_t m_index;
    Eigen::Affine3f m_pose = Eigen::Affine3f::Identity();
    Eigen::Affine3f m_raw_pose = Eigen::Affine3f::Identity();
    PointerState m_now;
    PointerState m_before;
    std::chrono::steady_clock::time_point m_last_click;
    Eigen::Affine3f m_tracked_pose = Eigen::Affine3f::Identity();
    PoseFilter m_grab_filter;
    PoseFilter m_aim_filter;
    InteractionState m_interaction;
};
