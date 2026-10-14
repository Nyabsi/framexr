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

#include "OneEuroFilter.hpp"

class RotationFilter final
{
  public:
    explicit RotationFilter(OneEuroSettings settings);

    auto Reset() -> void;
    auto Filter(const Eigen::Quaternionf& value, float delta_seconds) -> Eigen::Quaternionf;

  private:
    OneEuroSettings m_settings;
    std::optional<Eigen::Quaternionf> m_value;
    float m_speed = 0.0f;
};
