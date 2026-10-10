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

#include "OverlayLayout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Overlay.hpp"

namespace {

constexpr float kOverlapGap = 0.0175f;
constexpr int kMaxNudges = 16;

} // namespace

auto ViewRect::Overlaps(const ViewRect& other, float gap) const -> bool {
    return min_azimuth < other.max_azimuth + gap && other.min_azimuth < max_azimuth + gap
        && min_elevation < other.max_elevation + gap && other.min_elevation < max_elevation + gap;
}

auto OverlayLayout::Corners(Overlay& overlay) -> std::optional<std::array<Eigen::Vector3f, 4>> {
    const auto meta = overlay.backend->Meta();
    const VkExtent2D extent = meta && meta->extent.width > 0 && meta->extent.height > 0 ? meta->extent : overlay.state.extent_hint;
    if (extent.width == 0 || extent.height == 0) {
        return std::nullopt;
    }
    const Eigen::Matrix4f frame = meta ? meta->transform.matrix() : Eigen::Matrix4f::Identity();
    const Eigen::Matrix4f effective = overlay.state.transform.matrix() * frame;

    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const float half_width = std::min(aspect, 1.0f) / 2.0f;
    const float half_height = half_width / aspect;

    std::array<Eigen::Vector3f, 4> corners;
    const std::array<Eigen::Vector2f, 4> local{{
        {-half_width, -half_height},
        {half_width, -half_height},
        {half_width, half_height},
        {-half_width, half_height},
    }};
    for (size_t i = 0; i < corners.size(); ++i) {
        const Eigen::Vector4f point = effective * Eigen::Vector4f(local[i].x(), local[i].y(), 0.0f, 1.0f);
        corners[i] = point.head<3>();
    }
    return corners;
}

auto OverlayLayout::ViewBounds(const std::array<Eigen::Vector3f, 4>& corners, const Eigen::Affine3f& head) -> ViewRect {
    const Eigen::Matrix3f to_head = Eigen::Matrix3f(head.linear()).transpose();
    const Eigen::Vector3f origin = head.translation();
    ViewRect rect{
        .min_azimuth = std::numeric_limits<float>::max(),
        .max_azimuth = std::numeric_limits<float>::lowest(),
        .min_elevation = std::numeric_limits<float>::max(),
        .max_elevation = std::numeric_limits<float>::lowest(),
    };
    for (const auto& corner : corners) {
        const Eigen::Vector3f local = to_head * (corner - origin);
        const float azimuth = std::atan2(local.x(), -local.z());
        const float elevation = std::atan2(local.y(), std::hypot(local.x(), local.z()));
        rect.min_azimuth = std::min(rect.min_azimuth, azimuth);
        rect.max_azimuth = std::max(rect.max_azimuth, azimuth);
        rect.min_elevation = std::min(rect.min_elevation, elevation);
        rect.max_elevation = std::max(rect.max_elevation, elevation);
    }
    return rect;
}

auto OverlayLayout::RotateAroundHead(Overlay& overlay, const Eigen::Affine3f& head, const Eigen::Vector3f& axis, float angle)
    -> void {
    const Eigen::Matrix3f rotation = Eigen::AngleAxisf(angle, axis).toRotationMatrix();
    const Eigen::Vector3f pivot = head.translation();
    Eigen::Matrix4f around = Eigen::Matrix4f::Identity();
    around.topLeftCorner<3, 3>() = rotation;
    around.topRightCorner<3, 1>() = pivot - rotation * pivot;
    overlay.state.transform = Eigen::Affine3f(around * overlay.state.transform.matrix());
    overlay.state.dirty = true;
}

auto OverlayLayout::NudgeClear(Overlay& moving, std::span<Overlay* const> fixed, const Eigen::Affine3f& head) -> bool {
    const Eigen::Matrix3f head_rotation = head.linear();
    const Eigen::Vector3f up = head_rotation.col(1);
    const Eigen::Vector3f right = head_rotation.col(0);

    bool moved = false;
    for (int attempt = 0; attempt < kMaxNudges; ++attempt) {
        const auto moving_corners = Corners(moving);
        if (!moving_corners) {
            return moved;
        }
        const ViewRect rect = ViewBounds(*moving_corners, head);

        std::optional<ViewRect> blocker;
        for (auto* other : fixed) {
            if (other == &moving) {
                continue;
            }
            if (const auto corners = Corners(*other)) {
                if (const ViewRect bounds = ViewBounds(*corners, head); rect.Overlaps(bounds, kOverlapGap)) {
                    blocker = bounds;
                    break;
                }
            }
        }
        if (!blocker) {
            return moved;
        }

        const float left = blocker->min_azimuth - kOverlapGap - rect.max_azimuth;
        const float rightward = blocker->max_azimuth + kOverlapGap - rect.min_azimuth;
        const float down = blocker->min_elevation - kOverlapGap - rect.max_elevation;
        const float upward = blocker->max_elevation + kOverlapGap - rect.min_elevation;

        const float horizontal = std::abs(left) < std::abs(rightward) ? left : rightward;
        const float vertical = std::abs(down) < std::abs(upward) ? down : upward;
        if (std::abs(vertical) <= std::abs(horizontal)) {
            RotateAroundHead(moving, head, right, vertical);
        } else {
            RotateAroundHead(moving, head, up, -horizontal);
        }
        moved = true;
    }
    return moved;
}
