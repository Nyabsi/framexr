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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "Error.hpp"

enum class UdpAddress : uint8_t {
    Any,
    Loopback,
};

class UdpSocket final {
  public:
    static auto Bind(UdpAddress address, uint16_t port) -> Result<UdpSocket>;

    ~UdpSocket();
    UdpSocket(UdpSocket&& other) noexcept;
    auto operator=(UdpSocket&& other) noexcept -> UdpSocket&;
    UdpSocket(const UdpSocket&) = delete;
    auto operator=(const UdpSocket&) -> UdpSocket& = delete;

    auto ConnectLoopback(uint16_t port) -> Result<>;
    [[nodiscard]] auto LocalPort() const -> uint16_t;
    auto SetReceiveTimeout(std::chrono::milliseconds timeout) -> Result<>;
    [[nodiscard]] auto Send(std::span<const std::byte> data) const -> bool;
    [[nodiscard]] auto Receive(std::span<std::byte> buffer) const -> std::optional<size_t>;

  private:
    explicit UdpSocket(intptr_t handle);

    intptr_t m_handle;
};
