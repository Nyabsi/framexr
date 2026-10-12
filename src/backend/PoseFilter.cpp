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

#include "PoseFilter.hpp"

PoseFilter::PoseFilter(OneEuroSettings position, OneEuroSettings rotation) : m_position(position), m_rotation(rotation) {
}

auto PoseFilter::Reset() -> void {
    m_position.Reset();
    m_rotation.Reset();
}

auto PoseFilter::Filter(const Eigen::Affine3f& pose, float delta_seconds) -> Eigen::Affine3f {
    const Eigen::Matrix3f linear = pose.linear();
    Eigen::Affine3f filtered = Eigen::Affine3f::Identity();
    filtered.linear() = m_rotation.Filter(Eigen::Quaternionf(linear), delta_seconds).toRotationMatrix();
    filtered.translation() = m_position.Filter(pose.translation(), delta_seconds);
    return filtered;
}
