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

#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <Eigen/Core>
#include <nlohmann/json.hpp>

#include "Control.hpp"

class BatteryLabel final : public Control {
  public:
    BatteryLabel(size_t device, float low_threshold, Eigen::Vector4f normal, Eigen::Vector4f low, Eigen::Vector4f charging);

    auto Update() -> void override;

  private:
    size_t m_device;
    float m_low_threshold;
    Eigen::Vector4f m_normal_color;
    Eigen::Vector4f m_low_color;
    Eigen::Vector4f m_charging_color;
};
