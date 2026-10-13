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

#include "Devices.hpp"

#include <algorithm>
#include <tuple>

Devices* g_devices = nullptr;

namespace {

auto Contains(const std::vector<TrackedDevice>& devices, const TrackedDevice& device) -> bool {
    return std::ranges::any_of(devices, [&](const TrackedDevice& other) -> bool {
        return other.id == device.id && other.role == device.role;
    });
}

} // namespace

auto Devices::RoleName(TrackedDeviceRole role) -> std::string_view {
    switch (role) {
    case TrackedDeviceRole::None: return "Device";
    case TrackedDeviceRole::Hmd: return "Headset";
    case TrackedDeviceRole::LeftHand: return "Left controller";
    case TrackedDeviceRole::RightHand: return "Right controller";
    case TrackedDeviceRole::Tracker: return "Tracker";
    }
    return "Device";
}

auto Devices::Update(std::vector<TrackedDevice> devices) -> void {
    std::ranges::sort(devices, [](const TrackedDevice& a, const TrackedDevice& b) -> bool {
        const auto key = [](const TrackedDevice& device) -> std::tuple<int, uint8_t, float> {
            return std::tuple(device.soc.has_value() ? 0 : 1, static_cast<uint8_t>(device.role), device.soc.value_or(999.0f));
        };
        return key(a) < key(b);
    });
    if (m_known) {
        for (const auto& device : devices) {
            if (m_on_connect && !Contains(m_devices, device)) {
                m_on_connect(device);
            }
        }
        for (const auto& device : m_devices) {
            if (m_on_disconnect && !Contains(devices, device)) {
                m_on_disconnect(device);
            }
        }
    }
    m_devices = std::move(devices);
    m_known = true;
}
