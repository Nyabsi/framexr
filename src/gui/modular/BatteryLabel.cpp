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

#include "BatteryLabel.hpp"

#include <algorithm>
#include <array>
#include <format>

#include "Input.hpp"

namespace {

constexpr std::array<std::string_view, 5> kDeviceTags{"", "H", "L", "R", "T"};

} // namespace

BatteryLabel::BatteryLabel(size_t device, float low_threshold, Eigen::Vector4f normal, Eigen::Vector4f low, Eigen::Vector4f charging)
    : m_device(device),
      m_low_threshold(low_threshold),
      m_normal_color(std::move(normal)),
      m_low_color(std::move(low)),
      m_charging_color(std::move(charging)) {
}

auto BatteryLabel::Update() -> void {
    if (m_device >= g_input->Devices().size()) {
        SetText("");
        return;
    }
    const auto& device = g_input->Devices()[m_device];
    if (!device.soc) {
        SetText("");
        SetFgColor(Eigen::Vector4f::Zero());
        return;
    }
    const auto percent = static_cast<uint32_t>(std::min(*device.soc * 100.0f, 99.0f));
    SetText(std::format("{}{}", kDeviceTags[static_cast<size_t>(device.role)], percent));
    if (device.charging) {
        SetFgColor(m_charging_color);
    } else if (*device.soc < m_low_threshold) {
        SetFgColor(m_low_color);
    } else {
        SetFgColor(m_normal_color);
    }
}
