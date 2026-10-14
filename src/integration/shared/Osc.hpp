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
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "Error.hpp"
#include "Devices.hpp"
#include "UdpSocket.hpp"

using OscValue = std::variant<int32_t, float, std::string, bool>;

class Osc final
{
  public:
    static auto Create(uint16_t port) -> Result<std::unique_ptr<Osc>>;

    ~Osc();
    Osc(const Osc&) = delete;
    auto operator=(const Osc&) -> Osc& = delete;

    [[nodiscard]] auto SendMessage(const std::string& address, const std::vector<OscValue>& values) const -> Result<>;
    auto SendParams(const std::vector<TrackedDevice>& devices) -> Result<>;

  private:
    explicit Osc(UdpSocket socket);

    UdpSocket m_socket;
    std::chrono::steady_clock::time_point m_last_sent_overlay;
    std::chrono::steady_clock::time_point m_last_sent_battery;
};

extern Osc* g_osc;
