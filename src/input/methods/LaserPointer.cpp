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

#include "methods/LaserPointer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Config.hpp"
#include "Logger.hpp"
#include "Container.hpp"
#include "Raycast.hpp"

namespace {

constexpr float kGrabSlideRate = 4.0f;

constexpr float kMaxCurvature = 0.5f;

constexpr float kBendPerMeter = 1.5f;

constexpr OneEuroSettings kGrabPositionSmoothing{.min_cutoff = 1.5f, .beta = 10.0f, .derivative_cutoff = 1.0f};

constexpr OneEuroSettings kGrabRotationSmoothing{.min_cutoff = 1.5f, .beta = 3.0f, .derivative_cutoff = 1.0f};

constexpr OneEuroSettings kAimPositionSmoothing{.min_cutoff = 2.0f, .beta = 6.0f, .derivative_cutoff = 1.0f};

constexpr OneEuroSettings kAimRotationSmoothing{.min_cutoff = 1.0f, .beta = 1.5f, .derivative_cutoff = 1.0f};

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

auto Curvable(View& overlay) -> bool {
    const auto meta = overlay.Composer()->Meta();
    return meta && meta->extent.width >= meta->extent.height;
}

auto Bend(View& overlay, float curvature) -> void {
    const float clamped = std::min(curvature, kMaxCurvature);
    overlay.SetCurvature(clamped <= std::numeric_limits<float>::epsilon() ? std::nullopt : std::optional<float>(clamped));
}

} // namespace

LaserPointer::LaserPointer(size_t pointer_index)
    : m_index(pointer_index),
      m_last_click(std::chrono::steady_clock::now()),
      m_grab_filter(kGrabPositionSmoothing, kGrabRotationSmoothing),
      m_aim_filter(kAimPositionSmoothing, kAimRotationSmoothing) {
}

auto LaserPointer::LinePose(const Eigen::Affine3f& pose, const std::optional<Eigen::Vector3f>& target) -> Eigen::Affine3f {
    if (!target) {
        return pose;
    }
    const Eigen::Vector3f& aim = *target;
    const Eigen::Vector3f origin = pose.translation();
    const Eigen::Vector3f direction = aim - origin;
    if (direction.dot(direction) <= 0.0f) {
        return pose;
    }
    const Eigen::Matrix3f linear = pose.linear();
    const Eigen::Vector3f forward = linear * -Eigen::Vector3f::UnitZ();
    Eigen::Affine3f line = Eigen::Affine3f::Identity();
    const Eigen::Vector3f aim_direction = direction.normalized();
    const Eigen::Vector3f axis = forward.cross(aim_direction);
    const float turn = std::atan2(axis.norm(), forward.dot(aim_direction));
    Eigen::Vector3f turn_axis = linear.col(1);
    if (axis.norm() > 1e-6f) {
        turn_axis = axis.normalized();
    }
    line.linear() = Eigen::AngleAxisf(turn, turn_axis).toRotationMatrix() * linear;
    line.translation() = origin;
    return line;
}

auto LaserPointer::RayTest(OverlayId overlay, const Eigen::Affine3f& transform, std::optional<float> curvature) const
    -> std::optional<RayHit> {
    std::optional<std::pair<float, Eigen::Vector2f>> intersection;
    if (curvature) {
        intersection = Raycast::Cylinder(m_pose, -Eigen::Vector3f::UnitZ(), transform, *curvature);
    } else {
        intersection = Raycast::Plane(m_pose, -Eigen::Vector3f::UnitZ(), transform);
    }
    if (!intersection || intersection->first < 0.0f) {
        return std::nullopt;
    }
    return RayHit{
        .overlay = overlay,
        .global_position = m_pose * (-Eigen::Vector3f::UnitZ() * intersection->first),
        .local_position = intersection->second,
        .dist = intersection->first,
    };
}

auto LaserPointer::NearestHit() const -> std::optional<PointerHit> {
    std::vector<RayHit> hits;
    for (const auto& overlay : g_container->Views()) {
        if (!overlay || !overlay->WantVisible() || !overlay->Interactable()) {
            continue;
        }
        if (const auto hit = RayTest(overlay->Id(), overlay->Transform(), overlay->Curvature()); hit && std::isfinite(hit->dist)) {
            hits.push_back(*hit);
        }
    }

    std::ranges::sort(hits, [](const RayHit& a, const RayHit& b) -> bool { return a.dist < b.dist; });

    for (const auto& hit : hits) {
        const auto* overlay = g_container->ById(hit.overlay);
        if (overlay == nullptr) {
            continue;
        }
        const Eigen::Vector2f uv = overlay->InteractionTransform() * hit.local_position;
        if (uv.x() < 0.0f || uv.x() > 1.0f || uv.y() < 0.0f || uv.y() > 1.0f || !overlay->Accepts(uv)) {
            continue;
        }
        return PointerHit{
            .pointer = m_index,
            .overlay = hit.overlay,
            .mode = m_interaction.mode,
            .primary = false,
            .uv = uv,
            .dist = hit.dist,
        };
    }
    return std::nullopt;
}

auto LaserPointer::StartGrab(View& overlay, const Eigen::Vector3f& point) -> void {
    m_interaction.grabbed = GrabData{
        .offset = Eigen::Affine3f(m_pose.matrix().inverse()) * overlay.Transform(),
        .grabbed_id = overlay.Id(),
        .grab_point = Eigen::Affine3f(overlay.Transform().matrix().inverse()) * point,
        .old_curvature = overlay.Curvature(),
        .grab_all = m_interaction.mode == PointerMode::Right,
    };
    m_grab_filter.Reset();
    m_grab_filter.Filter(m_tracked_pose, 0.0f);
    LOG_INFO("Hand {}: grabbed {}", m_index, overlay.Name());
}

auto LaserPointer::HandleGrabbed(View& overlay) -> void {
    auto& state = overlay;
    if (m_now.grab) {
        if (!m_interaction.grabbed) {
            LOG_ERROR("Grabbed overlay {} does not exist", static_cast<uint64_t>(state.Id()));
            return;
        }
        auto& offset = m_interaction.grabbed->offset;
        const Eigen::Affine3f hand = g_config->grab_smoothing ? m_grab_filter.Filter(m_tracked_pose, g_interaction->DeltaSeconds())
                                                              : m_pose;
        auto& grabbed = *m_interaction.grabbed;
        const float slide = g_config->allow_sliding ? SlideAmount(m_now.scroll_y) : 0.0f;
        if (m_now.click && slide == 0.0f && Curvable(state)) {
            const float distance = (hand.translation() - g_interaction->Hmd().translation()).norm();
            if (!grabbed.bend_from) {
                grabbed.bend_from = distance;
                grabbed.bend_curvature = state.Curvature().value_or(0.0f);
            }
            Bend(state, std::fma(*grabbed.bend_from - distance, kBendPerMeter, grabbed.bend_curvature));
            offset = Eigen::Affine3f(hand.matrix().inverse()) * state.Transform();
            state.SetDirty(true);
            return;
        }
        grabbed.bend_from.reset();
        if (slide != 0.0f) {
            const float distance = offset.translation().norm();
            const Eigen::Matrix3f linear = offset.linear();
            const float scale = linear.col(0).norm();
            if (distance > 0.0f && scale > 0.0f) {
                const float wanted = std::clamp(
                    distance * std::exp(slide * kGrabSlideRate * g_interaction->DeltaSeconds()),
                    kMinGrabDistance,
                    kMaxGrabDistance
                );
                const Eigen::Vector3f head = g_interaction->Hmd().translation();
                const float old_reach = (hand * offset.translation() - head).norm();
                offset.translation() *= wanted / distance;
                const float new_reach = (hand * offset.translation() - head).norm();
                if (old_reach > 0.0f) {
                    offset.linear() = linear
                                    * std::clamp(new_reach / old_reach, kMinOverlayScale / scale, kMaxOverlayScale / scale);
                }
            }
        }
        state.SetTransform(hand * offset);
        state.SetDirty(true);
        return;
    }

    if (state.Recenter()) {
        g_container->RememberUserPlacement(overlay);
        const auto relative = state.RememberRelativeToHead();
        if (m_interaction.grabbed && state.Curvature() != m_interaction.grabbed->old_curvature) {
            if (const auto curvature = state.Curvature()) {
                SetNamed(g_config->curve_values, state.Name(), *curvature);
            } else {
                std::erase_if(g_config->curve_values, [&](const auto& entry) -> bool { return entry.first == state.Name(); });
            }
        }
        SetNamed(g_config->transform_values, state.Name(), relative);
    }

    m_interaction.grabbed.reset();
    LOG_INFO("Hand {}: dropped {}", m_index, state.Name());
}

auto LaserPointer::ReleaseClick(const std::optional<PointerHit>& hit) -> void {
    const auto clicked_id = std::exchange(m_interaction.clicked_id, std::nullopt);
    if (!clicked_id) {
        return;
    }
    if (auto* clicked = g_container->ById(*clicked_id)) {
        clicked->OnPointer(hit.value_or(PointerHit{.pointer = m_index, .overlay = *clicked_id, .mode = m_interaction.mode}), false);
    }
}

auto LaserPointer::Interact() -> InteractionResult {
    if (m_interaction.grabbed) {
        if (!m_now.click) {
            ReleaseClick(std::nullopt);
        }
        if (auto* grabbed = g_container->ById(m_interaction.grabbed->grabbed_id)) {
            HandleGrabbed(*grabbed);
            if (m_interaction.grabbed) {
                const Eigen::Vector3f point = grabbed->Transform() * m_interaction.grabbed->grab_point;
                const Eigen::Vector3f origin = m_pose.translation();
                const Eigen::Vector3f reach = point - origin;
                return {.length = std::sqrt(reach.dot(reach)), .target = point};
            }
        } else {
            LOG_WARN("Grabbed overlay {} does not exist", static_cast<uint64_t>(m_interaction.grabbed->grabbed_id));
            m_interaction.grabbed.reset();
        }
        return {.length = 0.1f};
    }

    auto hit = NearestHit();
    if (!hit) {
        if (const auto hovered_id = std::exchange(m_interaction.hovered_id, std::nullopt)) {
            if (auto* hovered = g_container->ById(*hovered_id)) {
                hovered->OnLeft(m_index);
            }
        }
        if (!m_now.click) {
            ReleaseClick(std::nullopt);
        }
        return {};
    }

    if (m_interaction.hovered_id && *m_interaction.hovered_id != hit->overlay) {
        if (auto* previous = g_container->ById(*m_interaction.hovered_id)) {
            if (previous->PrimaryPointer() == m_index) {
                previous->SetPrimaryPointer(std::nullopt);
            }
            previous->OnLeft(m_index);
        }
    }

    auto* hovered = g_container->ById(hit->overlay);
    if (hovered == nullptr) {
        LOG_WARN("Hit overlay {} does not exist", static_cast<uint64_t>(hit->overlay));
        return {};
    }
    m_interaction.hovered_id = hit->overlay;

    if (const auto& primary = hovered->PrimaryPointer(); !primary || hit->pointer <= *primary) {
        hovered->SetPrimaryPointer(hit->pointer);
        hit->primary = true;
    }

    LOG_TRACE("Hit: {} uv=({}, {}) dist={}", hovered->Name(), hit->uv.x(), hit->uv.y(), hit->dist);

    if (m_now.grab && !m_before.grab && hovered->Grabbable()) {
        StartGrab(*hovered, m_pose * (-Eigen::Vector3f::UnitZ() * hit->dist));
        return {.length = hit->dist, .haptics = Haptics{.intensity = 0.25f, .duration = 0.1f, .frequency = 0.1f}};
    }

    const auto haptics = hovered->OnHover(*hit);

    if (std::abs(m_now.scroll_x) > 0.1f || std::abs(m_now.scroll_y) > 0.1f) {
        const auto& other = g_interaction->Pointers()[1 - m_index];
        if (other.Interaction().grabbed && other.Interaction().grabbed->grabbed_id == hit->overlay) {
            if (Curvable(*hovered)) {
                Bend(*hovered, std::fma(m_now.scroll_y, -0.01f, hovered->Curvature().value_or(0.0f)));
            } else {
                hovered->Curvature().reset();
            }
        } else {
            hovered->OnScroll(*hit, m_now.scroll_y, m_now.scroll_x);
        }
    }

    if (m_now.click && !m_before.click) {
        m_interaction.clicked_id = hit->overlay;
        hovered->OnPointer(*hit, true);
    } else if (!m_now.click && m_interaction.clicked_id) {
        ReleaseClick(hit);
    } else if (!m_now.click && m_before.click) {
        hovered->OnPointer(*hit, false);
    }
    return {.length = hit->dist, .haptics = haptics};
}
