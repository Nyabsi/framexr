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

#include <cmath>
#include <numbers>
#include <optional>
#include <random>
#include <utility>

#include <gtest/gtest.h>

#include "Raycast.hpp"

namespace
{

auto Linear(const Eigen::Affine3f& transform) -> Eigen::Matrix3f
{
    return transform.linear();
}

auto ReferencePlane(const Eigen::Affine3f& source, const Eigen::Vector3f& source_fwd, const Eigen::Affine3f& plane)
    -> std::pair<float, Eigen::Vector2f>
{
    const Eigen::Vector3f plane_normal = Linear(plane) * -Eigen::Vector3f::UnitZ();
    const Eigen::Vector3f ray_dir = Linear(source) * source_fwd;
    const float d = plane.translation().dot(-plane_normal);
    const float dist = -(d + source.translation().dot(plane_normal)) / ray_dir.dot(plane_normal);
    const Eigen::Vector3f hit_world = source.translation() + ray_dir * dist;
    const Eigen::Matrix3f plane_linear = Linear(plane);
    const Eigen::Vector3f hit_local = plane_linear.inverse() * (hit_world - plane.translation());
    return {dist, hit_local.head<2>()};
}

auto ReferenceCylinder(
    const Eigen::Affine3f& source,
    const Eigen::Vector3f& source_fwd,
    const Eigen::Affine3f& plane,
    float curvature
) -> std::optional<std::pair<float, Eigen::Vector2f>>
{
    const float size = Linear(plane).col(0).norm();
    const Eigen::Matrix3f scaled_linear = Linear(plane) / size;
    const Eigen::Matrix3f to_local = scaled_linear.inverse();

    const float radius = size / (2.0f * std::numbers::pi_v<float> * curvature);
    const Eigen::Vector3f ray_dir = to_local * (Linear(source) * source_fwd);
    const Eigen::Vector3f ray_origin = to_local * (source.translation() - plane.translation()) - Eigen::Vector3f::UnitZ() * radius;

    const Eigen::Vector2f v_dir(ray_dir.x(), ray_dir.z());
    const Eigen::Vector2f v_pos(ray_origin.x(), ray_origin.z());
    const float l_dir = v_dir.dot(v_dir);
    const float l_pos = v_dir.dot(v_pos);
    const float c = std::fma(radius, -radius, v_pos.dot(v_pos));
    const float d = std::fma(l_pos, l_pos, -(l_dir * c));
    if (d < std::numeric_limits<float>::epsilon())
    {
        return std::nullopt;
    }
    const float sqrt_d = std::sqrt(d);
    const float t = std::max((-l_pos - sqrt_d) / l_dir, (-l_pos + sqrt_d) / l_dir);
    if (t < std::numeric_limits<float>::epsilon())
    {
        return std::nullopt;
    }
    Eigen::Vector3f hit_local = ray_origin + ray_dir * t;
    if (hit_local.z() > 0.0f)
    {
        return std::nullopt;
    }
    const float max_angle = 2.0f * (size / (2.0f * radius));
    const float x_angle = std::asin(hit_local.x() / radius);
    hit_local.x() = x_angle / max_angle;
    hit_local.y() /= size;
    return std::pair{t, Eigen::Vector2f(hit_local.x(), hit_local.y())};
}

auto RandomPose(std::mt19937& rng, float spread, float min_scale, float max_scale) -> Eigen::Affine3f
{
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    std::uniform_real_distribution<float> scale(min_scale, max_scale);
    const Eigen::Quaternionf rotation = Eigen::Quaternionf(unit(rng), unit(rng), unit(rng), unit(rng)).normalized();
    Eigen::Affine3f pose = Eigen::Affine3f::Identity();
    pose.translate(Eigen::Vector3f(unit(rng), unit(rng), unit(rng)) * spread);
    pose.rotate(rotation);
    pose.scale(scale(rng));
    return pose;
}

auto Near(float actual, float expected) -> bool
{
    return std::abs(actual - expected) <= 1e-3f * std::max(1.0f, std::abs(expected));
}

} // namespace

TEST(Raycast, PlaneMatchesOriginalImplementation)
{
    std::mt19937 rng(1234);
    int compared = 0;
    for (int i = 0; i < 5000; ++i)
    {
        const auto source = RandomPose(rng, 1.0f, 1.0f, 1.0f);
        const auto plane = RandomPose(rng, 3.0f, 0.2f, 3.0f);
        const auto [expected_dist, expected_local] = ReferencePlane(source, -Eigen::Vector3f::UnitZ(), plane);
        if (!std::isfinite(expected_dist) || std::abs(expected_dist) > 1e3f)
        {
            continue;
        }
        const auto [dist, local] = Raycast::Plane(source, -Eigen::Vector3f::UnitZ(), plane);
        ASSERT_TRUE(Near(dist, expected_dist)) << "case " << i << ": " << dist << " vs " << expected_dist;
        ASSERT_TRUE(Near(local.x(), expected_local.x()) && Near(local.y(), expected_local.y()))
            << "case " << i << ": (" << local.transpose() << ") vs (" << expected_local.transpose() << ")";
        ++compared;
    }
    EXPECT_GT(compared, 4000);
}

TEST(Raycast, CylinderMatchesOriginalImplementation)
{
    std::mt19937 rng(5678);
    std::uniform_real_distribution<float> curvature(0.05f, 0.5f);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    int hits = 0;
    for (int i = 0; i < 5000; ++i)
    {
        const auto source = RandomPose(rng, 0.5f, 1.0f, 1.0f);
        Eigen::Affine3f plane = Eigen::Affine3f::Identity();
        plane.translate(Eigen::Vector3f(0.3f * unit(rng), 0.3f * unit(rng), -1.5f + 0.3f * unit(rng)));
        plane.rotate(Eigen::AngleAxisf(0.3f * unit(rng), Eigen::Vector3f::UnitY()));
        plane.scale(1.0f + std::abs(unit(rng)));
        const float bend = curvature(rng);

        const auto expected = ReferenceCylinder(source, -Eigen::Vector3f::UnitZ(), plane, bend);
        const auto actual = Raycast::Cylinder(source, -Eigen::Vector3f::UnitZ(), plane, bend);
        ASSERT_EQ(actual.has_value(), expected.has_value()) << "case " << i;
        if (!expected)
        {
            continue;
        }
        ASSERT_TRUE(Near(actual->first, expected->first)) << "case " << i << ": " << actual->first << " vs " << expected->first;
        ASSERT_TRUE(Near(actual->second.x(), expected->second.x()) && Near(actual->second.y(), expected->second.y()))
            << "case " << i;
        ++hits;
    }
    EXPECT_GT(hits, 500);
}
