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

#include "OneEuroFilter.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

constexpr float kMinDeltaSeconds = 1e-4f;

} // namespace

OneEuroFilter::OneEuroFilter(OneEuroSettings settings) : settings_(settings) {
}

auto OneEuroFilter::Alpha(float cutoff, float delta_seconds) -> float {
    const float tau = 1.0f / (2.0f * std::numbers::pi_v<float> * cutoff);
    return 1.0f / (1.0f + tau / std::max(delta_seconds, kMinDeltaSeconds));
}

auto OneEuroFilter::Reset() -> void {
    value_.reset();
    derivative_ = Eigen::Vector3f::Zero();
}

auto OneEuroFilter::Filter(const Eigen::Vector3f& value, float delta_seconds) -> Eigen::Vector3f {
    if (!value_) {
        value_ = value;
        return value;
    }
    const float delta = std::max(delta_seconds, kMinDeltaSeconds);
    const Eigen::Vector3f raw_derivative = (value - *value_) / delta;
    derivative_ += Alpha(settings_.derivative_cutoff, delta) * (raw_derivative - derivative_);
    const float cutoff = std::fma(settings_.beta, derivative_.norm(), settings_.min_cutoff);
    *value_ += Alpha(cutoff, delta) * (value - *value_);
    return *value_;
}

RotationFilter::RotationFilter(OneEuroSettings settings) : settings_(settings) {
}

auto RotationFilter::Reset() -> void {
    value_.reset();
    speed_ = 0.0f;
}

auto RotationFilter::Filter(const Eigen::Quaternionf& value, float delta_seconds) -> Eigen::Quaternionf {
    if (!value_) {
        value_ = value.normalized();
        return *value_;
    }
    const float delta = std::max(delta_seconds, kMinDeltaSeconds);
    const float raw_speed = value_->angularDistance(value) / delta;
    speed_ += OneEuroFilter::Alpha(settings_.derivative_cutoff, delta) * (raw_speed - speed_);
    const float cutoff = std::fma(settings_.beta, speed_, settings_.min_cutoff);
    value_ = value_->slerp(OneEuroFilter::Alpha(cutoff, delta), value).normalized();
    return *value_;
}

PoseFilter::PoseFilter(OneEuroSettings position, OneEuroSettings rotation) : position_(position), rotation_(rotation) {
}

auto PoseFilter::Reset() -> void {
    position_.Reset();
    rotation_.Reset();
}

auto PoseFilter::Filter(const Eigen::Affine3f& pose, float delta_seconds) -> Eigen::Affine3f {
    const Eigen::Matrix3f linear = pose.linear();
    Eigen::Affine3f filtered = Eigen::Affine3f::Identity();
    filtered.linear() = rotation_.Filter(Eigen::Quaternionf(linear), delta_seconds).toRotationMatrix();
    filtered.translation() = position_.Filter(pose.translation(), delta_seconds);
    return filtered;
}
