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

#include "OpenXrPose.hpp"

#include <cmath>

auto OpenXrPose::FromTranslationRotation(const Eigen::Vector3f& translation, const Eigen::Quaternionf& rotation) -> XrPosef
{
    const Eigen::Quaternionf valid = rotation.coeffs().allFinite() ? rotation : Eigen::Quaternionf::Identity();
    return XrPosef{
        .orientation = {.x = valid.x(), .y = valid.y(), .z = valid.z(), .w = valid.w()},
        .position = {.x = translation.x(), .y = translation.y(), .z = translation.z()},
    };
}

auto OpenXrPose::FromTransform(const Eigen::Affine3f& transform) -> XrPosef
{
    return FromTranslationRotation(transform.translation(), NormalizedRotation(transform));
}

auto OpenXrPose::ToQuaternion(const XrQuaternionf& orientation) -> Eigen::Quaternionf
{
    return {orientation.w, orientation.x, orientation.y, orientation.z};
}

auto OpenXrPose::ToVector(const XrVector3f& position) -> Eigen::Vector3f
{
    return {position.x, position.y, position.z};
}

auto OpenXrPose::ToTransform(const XrPosef& pose) -> Eigen::Affine3f
{
    Eigen::Affine3f transform = Eigen::Affine3f::Identity();
    transform.linear() = ToQuaternion(pose.orientation).toRotationMatrix();
    transform.translation() = ToVector(pose.position);
    return transform;
}

auto OpenXrPose::NormalizedRotation(const Eigen::Affine3f& transform) -> Eigen::Quaternionf
{
    const Eigen::Matrix3f linear = transform.linear();
    const Eigen::Matrix3f normalized = linear / linear.col(0).norm();
    return Eigen::Quaternionf(normalized).normalized();
}

auto OpenXrPose::Lerp(const Eigen::Quaternionf& from, const Eigen::Quaternionf& to, float factor) -> Eigen::Quaternionf
{
    const float bias = from.dot(to) >= 0.0f ? 1.0f : -1.0f;
    const Eigen::Vector4f blended = from.coeffs() * (1.0f - factor) + to.coeffs() * bias * factor;
    return Eigen::Quaternionf(blended.normalized());
}

auto OpenXrPose::Ipd(std::span<const XrView> views) -> float
{
    const float distance = (ToVector(views[0].pose.position) - ToVector(views[1].pose.position)).norm();
    return std::round(distance * 10000.0f) * 0.1f;
}
