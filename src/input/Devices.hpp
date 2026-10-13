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

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

enum class TrackedDeviceRole : uint8_t {
    None,
    Hmd,
    LeftHand,
    RightHand,
    Tracker,
};

struct TrackedDevice {
    uint32_t id = 0;
    std::optional<float> soc;
    bool charging = false;
    TrackedDeviceRole role = TrackedDeviceRole::None;
};

using DeviceSignal = std::function<void(const TrackedDevice&)>;

class Devices final {
  public:
    [[nodiscard]] static auto RoleName(TrackedDeviceRole role) -> std::string_view;

    [[nodiscard]] auto All() const -> const std::vector<TrackedDevice>& { return m_devices; }

    auto SetOnConnect(DeviceSignal on_connect) -> void { m_on_connect = std::move(on_connect); }
    auto SetOnDisconnect(DeviceSignal on_disconnect) -> void { m_on_disconnect = std::move(on_disconnect); }

    auto Update(std::vector<TrackedDevice> devices) -> void;

  private:
    std::vector<TrackedDevice> m_devices;
    bool m_known = false;
    DeviceSignal m_on_connect;
    DeviceSignal m_on_disconnect;
};

extern Devices* g_devices;
