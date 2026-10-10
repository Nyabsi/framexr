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

#include "Input.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "Logger.hpp"
#include "OverlayContainer.hpp"
#include "State.hpp"

namespace {

constexpr float kGrabSlideRate = 4.0f;
constexpr OneEuroSettings kGrabPositionSmoothing{.min_cutoff = 1.5f, .beta = 10.0f, .derivative_cutoff = 1.0f};
constexpr OneEuroSettings kGrabRotationSmoothing{.min_cutoff = 1.5f, .beta = 3.0f, .derivative_cutoff = 1.0f};
constexpr float kMaxDeltaSeconds = 0.1f;
constexpr float kGrabSlideDeadzone = 0.2f;
constexpr float kMinGrabDistance = 0.1f;
constexpr float kMaxGrabDistance = 20.0f;
constexpr float kMinOverlayScale = 0.1f;
constexpr float kMaxOverlayScale = 20.0f;

auto SlideAmount(float stick) -> float {
    if (!std::isfinite(stick)) {
        return 0.0f;
    }
    const float beyond = std::max(std::abs(stick) - kGrabSlideDeadzone, 0.0f) / (1.0f - kGrabSlideDeadzone);
    return std::copysign(beyond, stick);
}

template <typename Value>
auto SetNamed(std::vector<std::pair<std::string, Value>>& entries, const std::string& name, Value value) -> void {
    std::erase_if(entries, [&](const auto& entry) -> bool { return entry.first == name; });
    entries.emplace_back(name, std::move(value));
}

} // namespace

Pointer::Pointer(size_t pointer_index)
    : index(pointer_index),
      last_click(std::chrono::steady_clock::now()),
      grab_filter(kGrabPositionSmoothing, kGrabRotationSmoothing) {
}

auto Pointer::RayTest(OverlayId overlay, const Eigen::Affine3f& transform, std::optional<float> curvature) const
    -> std::optional<RayHit> {
    std::optional<std::pair<float, Eigen::Vector2f>> intersection;
    if (curvature) {
        intersection = Raycast::Cylinder(pose, -Eigen::Vector3f::UnitZ(), transform, *curvature);
    } else {
        intersection = Raycast::Plane(pose, -Eigen::Vector3f::UnitZ(), transform);
    }
    if (!intersection || intersection->first < 0.0f) {
        return std::nullopt;
    }
    return RayHit{
        .overlay = overlay,
        .global_position = pose * (-Eigen::Vector3f::UnitZ() * intersection->first),
        .local_position = intersection->second,
        .dist = intersection->first,
    };
}

auto Pointer::NearestHit(OverlayContainer& overlays) const -> std::optional<PointerHit> {
    std::vector<RayHit> hits;
    for (const auto& overlay : overlays.Overlays()) {
        if (!overlay || !overlay->state.want_visible || !overlay->state.interactable) {
            continue;
        }
        if (const auto hit = RayTest(overlay->state.id, overlay->state.transform, overlay->state.curvature);
            hit && std::isfinite(hit->dist)) {
            hits.push_back(*hit);
        }
    }

    std::ranges::sort(hits, [](const RayHit& a, const RayHit& b) -> bool { return a.dist < b.dist; });

    for (const auto& hit : hits) {
        const auto* overlay = overlays.ById(hit.overlay);
        if (overlay == nullptr) {
            continue;
        }
        const Eigen::Vector2f uv = overlay->state.interaction_transform * hit.local_position;
        if (uv.x() < 0.0f || uv.x() > 1.0f || uv.y() < 0.0f || uv.y() > 1.0f) {
            continue;
        }
        return PointerHit{
            .pointer = index,
            .overlay = hit.overlay,
            .mode = interaction.mode,
            .primary = false,
            .uv = uv,
            .dist = hit.dist,
        };
    }
    return std::nullopt;
}

auto Pointer::StartGrab(Overlay& overlay) -> void {
    interaction.grabbed = GrabData{
        .offset = Eigen::Affine3f(pose.matrix().inverse()) * overlay.state.transform,
        .grabbed_id = overlay.state.id,
        .old_curvature = overlay.state.curvature,
        .grab_all = interaction.mode == PointerMode::Right,
    };
    grab_filter.Reset();
    grab_filter.Filter(pose, 0.0f);
    LOG_INFO("Hand {}: grabbed {}", index, overlay.state.name);
}

auto Pointer::HandleGrabbed(AppState& app, OverlayContainer& overlays, Overlay& overlay) -> void {
    auto& state = overlay.state;
    if (now.grab) {
        if (!interaction.grabbed) {
            LOG_ERROR("Grabbed overlay {} does not exist", static_cast<uint64_t>(state.id));
            return;
        }
        auto& offset = interaction.grabbed->offset;
        const Eigen::Affine3f hand = app.config.grab_smoothing ? grab_filter.Filter(pose, app.input.delta_seconds) : pose;
        if (const float slide = SlideAmount(now.scroll_y); app.config.allow_sliding && slide != 0.0f) {
            const float distance = offset.translation().norm();
            const Eigen::Matrix3f linear = offset.linear();
            const float scale = linear.col(0).norm();
            if (distance > 0.0f && scale > 0.0f) {
                const float wanted = std::clamp(
                    distance * std::exp(slide * kGrabSlideRate * app.input.delta_seconds),
                    kMinGrabDistance,
                    kMaxGrabDistance
                );
                const Eigen::Vector3f head = app.input.hmd.translation();
                const float old_reach = (hand * offset.translation() - head).norm();
                offset.translation() *= wanted / distance;
                const float new_reach = (hand * offset.translation() - head).norm();
                if (old_reach > 0.0f) {
                    offset.linear() = linear
                                    * std::clamp(new_reach / old_reach, kMinOverlayScale / scale, kMaxOverlayScale / scale);
                }
            }
        }
        state.transform = hand * offset;
        state.dirty = true;
        return;
    }

    if (state.recenter) {
        overlays.RememberUserPlacement(app, overlay);
        const auto relative = state.RememberRelativeToHead(app);
        if (interaction.grabbed && state.curvature != interaction.grabbed->old_curvature) {
            if (state.curvature) {
                SetNamed(app.config.curve_values, state.name, *state.curvature);
            } else {
                std::erase_if(app.config.curve_values, [&](const auto& entry) -> bool { return entry.first == state.name; });
            }
        }
        SetNamed(app.config.transform_values, state.name, relative);
    }

    interaction.grabbed.reset();
    LOG_INFO("Hand {}: dropped {}", index, state.name);
}

auto Pointer::Interact(AppState& app, OverlayContainer& overlays) -> InteractionResult {
    if (interaction.grabbed) {
        if (auto* grabbed = overlays.ById(interaction.grabbed->grabbed_id)) {
            HandleGrabbed(app, overlays, *grabbed);
        } else {
            LOG_WARN("Grabbed overlay {} does not exist", static_cast<uint64_t>(interaction.grabbed->grabbed_id));
            interaction.grabbed.reset();
        }
        return {.length = 0.1f};
    }

    auto hit = NearestHit(overlays);
    if (!hit) {
        if (const auto hovered_id = std::exchange(interaction.hovered_id, std::nullopt)) {
            if (auto* hovered = overlays.ById(*hovered_id)) {
                hovered->backend->OnLeft(app, index);
            }
        }
        if (!now.click && before.click) {
            if (const auto clicked_id = std::exchange(interaction.clicked_id, std::nullopt)) {
                if (auto* clicked = overlays.ById(*clicked_id)) {
                    clicked->backend->OnPointer(app, {.pointer = index, .overlay = *clicked_id, .mode = interaction.mode}, false);
                }
            }
        }
        return {};
    }

    if (interaction.hovered_id && *interaction.hovered_id != hit->overlay) {
        if (auto* previous = overlays.ById(*interaction.hovered_id)) {
            if (previous->primary_pointer == index) {
                previous->primary_pointer.reset();
            }
            previous->backend->OnLeft(app, index);
        }
    }

    auto* hovered = overlays.ById(hit->overlay);
    if (hovered == nullptr) {
        LOG_WARN("Hit overlay {} does not exist", static_cast<uint64_t>(hit->overlay));
        return {};
    }
    interaction.hovered_id = hit->overlay;

    if (!hovered->primary_pointer || hit->pointer <= *hovered->primary_pointer) {
        hovered->primary_pointer = hit->pointer;
        hit->primary = true;
    }

    LOG_TRACE("Hit: {} uv=({}, {}) dist={}", hovered->state.name, hit->uv.x(), hit->uv.y(), hit->dist);

    if (now.grab && !before.grab && hovered->state.grabbable) {
        StartGrab(*hovered);
        return {.length = hit->dist, .haptics = Haptics{.intensity = 0.25f, .duration = 0.1f, .frequency = 0.1f}};
    }

    const auto haptics = hovered->backend->OnHover(app, *hit);

    if (std::abs(now.scroll_x) > 0.1f || std::abs(now.scroll_y) > 0.1f) {
        const auto& other = app.input.pointers[1 - index];
        if (other.interaction.grabbed && other.interaction.grabbed->grabbed_id == hit->overlay) {
            const auto meta = hovered->backend->Meta();
            if (meta && meta->extent.width >= meta->extent.height) {
                const float curvature = std::min(std::fma(now.scroll_y, -0.01f, hovered->state.curvature.value_or(0.0f)), 0.5f);
                hovered->state.curvature = curvature <= std::numeric_limits<float>::epsilon() ? std::nullopt
                                                                                              : std::optional<float>(curvature);
            } else {
                hovered->state.curvature.reset();
            }
        } else {
            hovered->backend->OnScroll(app, *hit, now.scroll_y, now.scroll_x);
        }
    }

    if (now.click && !before.click) {
        interaction.clicked_id = hit->overlay;
        hovered->backend->OnPointer(app, *hit, true);
    } else if (!now.click && before.click) {
        if (const auto clicked_id = std::exchange(interaction.clicked_id, std::nullopt)) {
            if (auto* clicked = overlays.ById(*clicked_id)) {
                clicked->backend->OnPointer(app, *hit, false);
            }
        } else {
            hovered->backend->OnPointer(app, *hit, false);
        }
    }
    return {.length = hit->dist, .haptics = haptics};
}

InputState::InputState() : pointers{Pointer(0), Pointer(1)} {
}

auto InputState::PreUpdate() -> void {
    const auto now = std::chrono::steady_clock::now();
    delta_seconds = std::min(std::chrono::duration<float>(now - time).count(), kMaxDeltaSeconds);
    time = now;
    for (auto& pointer : pointers) {
        pointer.before = pointer.now;
    }
}

auto InputState::LogChanges(const Pointer& pointer) const -> void {
    const auto log = [&](std::string_view name, bool now, bool before) -> void {
        if (now != before) {
            LOG_DEBUG("Hand {}: {} {}", pointer.index, name, now);
        }
    };
    log("click", pointer.now.click, pointer.before.click);
    log("grab", pointer.now.grab, pointer.before.grab);
    log("alt_click", pointer.now.alt_click, pointer.before.alt_click);
    log("space_drag", pointer.now.space_drag, pointer.before.space_drag);
    log("space_rotate", pointer.now.space_rotate, pointer.before.space_rotate);
    log("space_reset", pointer.now.space_reset, pointer.before.space_reset);
    log("click_modifier_right", pointer.now.click_modifier_right, pointer.before.click_modifier_right);
    log("click_modifier_middle", pointer.now.click_modifier_middle, pointer.before.click_modifier_middle);
}

auto InputState::PostUpdate(const Config& config) -> void {
    for (auto& pointer : pointers) {
        LogChanges(pointer);

        if (pointer.now.click) {
            pointer.last_click = std::chrono::steady_clock::now();
        }
        if (pointer.now.click_modifier_right) {
            pointer.interaction.mode = PointerMode::Right;
            continue;
        }
        if (pointer.now.click_modifier_middle) {
            pointer.interaction.mode = PointerMode::Middle;
            continue;
        }
        pointer.interaction.mode = PointerMode::Left;

        if (pointer.now.alt_click == pointer.before.alt_click) {
            continue;
        }
        std::erase_if(processes_, [](Process& process) -> bool {
            const auto status = process.TryWait();
            return status && status->has_value();
        });
        const auto& command = pointer.now.alt_click ? config.alt_click_down : config.alt_click_up;
        if (!command.empty()) {
            if (auto process = Process::Spawn(command)) {
                processes_.push_back(std::move(*process));
            }
        }
    }
}

auto InputState::SortDevices() -> void {
    std::ranges::sort(devices, [](const TrackedDevice& a, const TrackedDevice& b) -> bool {
        const auto key = [](const TrackedDevice& device) -> std::tuple<int, uint8_t, float> {
            return std::tuple(device.soc.has_value() ? 0 : 1, static_cast<uint8_t>(device.role), device.soc.value_or(999.0f));
        };
        return key(a) < key(b);
    });
}

auto InputState::Interact(AppState& app, OverlayContainer& overlays) -> std::array<InteractionResult, 2> {
    auto& [left, right] = app.input.pointers;
    if (right.last_click > left.last_click) {
        auto right_result = right.Interact(app, overlays);
        auto left_result = left.Interact(app, overlays);
        return {left_result, right_result};
    }
    auto left_result = left.Interact(app, overlays);
    auto right_result = right.Interact(app, overlays);
    return {left_result, right_result};
}

auto Raycast::Plane(const Eigen::Affine3f& source, const Eigen::Vector3f& source_forward, const Eigen::Affine3f& plane)
    -> std::pair<float, Eigen::Vector2f> {
    const Eigen::Matrix3f plane_linear = plane.linear();
    const Eigen::Matrix3f source_linear = source.linear();
    const Eigen::Vector3f plane_origin = plane.translation();
    const Eigen::Vector3f source_origin = source.translation();

    const Eigen::Vector3f plane_normal = plane_linear * -Eigen::Vector3f::UnitZ();
    const Eigen::Vector3f ray_direction = source_linear * source_forward;

    const float d = plane_origin.dot(-plane_normal);
    const float dist = -(d + source_origin.dot(plane_normal)) / ray_direction.dot(plane_normal);
    const Eigen::Vector3f hit = source_origin + ray_direction * dist;
    const Eigen::Vector3f hit_local = plane_linear.inverse() * (hit - plane_origin);
    return {dist, Eigen::Vector2f(hit_local.x(), hit_local.y())};
}

auto Raycast::Cylinder(
    const Eigen::Affine3f& source,
    const Eigen::Vector3f& source_forward,
    const Eigen::Affine3f& plane,
    float curvature
) -> std::optional<std::pair<float, Eigen::Vector2f>> {
    const Eigen::Matrix3f plane_linear = plane.linear();
    const Eigen::Matrix3f source_linear = source.linear();
    const Eigen::Vector3f plane_origin = plane.translation();
    const Eigen::Vector3f source_origin = source.translation();

    const float size = plane_linear.col(0).norm();
    const Eigen::Matrix3f to_local = (plane_linear / size).inverse();
    const float radius = size / (2.0f * std::numbers::pi_v<float> * curvature);

    const Eigen::Vector3f ray_direction = to_local * (source_linear * source_forward);
    const Eigen::Vector3f ray_origin = to_local * (source_origin - plane_origin) - Eigen::Vector3f::UnitZ() * radius;

    const Eigen::Vector2f direction_xz(ray_direction.x(), ray_direction.z());
    const Eigen::Vector2f origin_xz(ray_origin.x(), ray_origin.z());

    const float length_direction = direction_xz.dot(direction_xz);
    const float length_origin = direction_xz.dot(origin_xz);
    const float c = std::fma(radius, -radius, origin_xz.dot(origin_xz));

    const float discriminant = std::fma(length_origin, length_origin, -(length_direction * c));
    if (discriminant < std::numeric_limits<float>::epsilon()) {
        return std::nullopt;
    }

    const float sqrt_discriminant = std::sqrt(discriminant);
    const float t1 = (-length_origin - sqrt_discriminant) / length_direction;
    const float t2 = (-length_origin + sqrt_discriminant) / length_direction;
    const float t = std::max(t1, t2);
    if (t < std::numeric_limits<float>::epsilon()) {
        return std::nullopt;
    }

    const Eigen::Vector3f hit_local = ray_origin + ray_direction * t;
    if (hit_local.z() > 0.0f) {
        return std::nullopt;
    }

    const float max_angle = 2.0f * (size / (2.0f * radius));
    const float x_angle = std::asin(hit_local.x() / radius);
    return std::pair{t, Eigen::Vector2f(x_angle / max_angle, hit_local.y() / size)};
}
