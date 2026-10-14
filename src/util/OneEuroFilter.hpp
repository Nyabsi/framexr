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

#pragma once

#include <optional>

#include <Eigen/Geometry>

struct OneEuroSettings
{
    float min_cutoff = 1.0f;
    float beta = 0.0f;
    float derivative_cutoff = 1.0f;
};

class OneEuroFilter final
{
  public:
    static constexpr float kMinDeltaSeconds = 1e-4f;

    explicit OneEuroFilter(OneEuroSettings settings);

    static auto Alpha(float cutoff, float delta_seconds) -> float;

    auto Reset() -> void;
    auto Filter(const Eigen::Vector3f& value, float delta_seconds) -> Eigen::Vector3f;

  private:
    OneEuroSettings m_settings;
    std::optional<Eigen::Vector3f> m_value;
    Eigen::Vector3f m_derivative = Eigen::Vector3f::Zero();
};
