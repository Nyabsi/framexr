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
#include <optional>
#include <vector>

#include <Eigen/Geometry>

#include "Process.hpp"

class AppState;
class Overlay;
class OverlayContainer;
struct Config;
struct OverlayState;

enum class OverlayId : uint64_t {};

enum class TrackedDeviceRole : uint8_t {
    None,
    Hmd,
    LeftHand,
    RightHand,
    Tracker,
};

struct TrackedDevice {
    std::optional<float> soc;
    bool charging = false;
    TrackedDeviceRole role = TrackedDeviceRole::None;
};

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
    bool show_hide = false;
    bool space_drag = false;
    bool space_rotate = false;
    bool space_reset = false;
    bool click_modifier_right = false;
    bool click_modifier_middle = false;
    bool move_mouse = false;
};

struct GrabData {
    Eigen::Vector3f offset = Eigen::Vector3f::Zero();
    OverlayId grabbed_id{};
    std::optional<float> old_curvature;
    bool grab_all = false;
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
};

class Pointer {
  public:
    explicit Pointer(size_t index);

    size_t index;
    Eigen::Affine3f pose = Eigen::Affine3f::Identity();
    Eigen::Affine3f raw_pose = Eigen::Affine3f::Identity();
    PointerState now;
    PointerState before;
    std::chrono::steady_clock::time_point last_click;
    InteractionState interaction;

    auto Interact(AppState& app, OverlayContainer& overlays) -> InteractionResult;

  private:
    [[nodiscard]] auto NearestHit(OverlayContainer& overlays) const -> std::optional<PointerHit>;
    [[nodiscard]] auto RayTest(OverlayId overlay, const Eigen::Affine3f& transform, std::optional<float> curvature) const
        -> std::optional<RayHit>;
    auto StartGrab(AppState& app, Overlay& overlay) -> void;
    auto HandleGrabbed(AppState& app, Overlay& overlay) -> void;
};

class InputState {
  public:
    InputState();

    Eigen::Affine3f hmd = Eigen::Affine3f::Identity();
    float ipd = 0.0f;
    std::array<Pointer, 2> pointers;
    std::vector<TrackedDevice> devices;

    auto PreUpdate() -> void;
    auto PostUpdate(const Config& config) -> void;
    auto SortDevices() -> void;

    static auto Interact(AppState& app, OverlayContainer& overlays) -> std::array<InteractionResult, 2>;

  private:
    auto LogChanges(const Pointer& pointer) const -> void;

    std::vector<Process> processes_;
};

class Raycast {
  public:
    static auto Plane(const Eigen::Affine3f& source, const Eigen::Vector3f& source_forward, const Eigen::Affine3f& plane)
        -> std::pair<float, Eigen::Vector2f>;
    static auto Cylinder(
        const Eigen::Affine3f& source,
        const Eigen::Vector3f& source_forward,
        const Eigen::Affine3f& plane,
        float curvature
    ) -> std::optional<std::pair<float, Eigen::Vector2f>>;
    static auto SnapUpright(const Eigen::Affine3f& transform, const Eigen::Vector3f& up) -> Eigen::Affine3f;
};
