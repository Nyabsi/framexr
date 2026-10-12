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

#include "Raycast.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

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
