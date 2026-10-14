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

#include "UdpSocket.hpp"

#include <cerrno>
#include <cstring>
#include <format>
#include <utility>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr intptr_t kInvalid = -1;

auto LastError() -> std::string {
    return std::format("{} (os error {})", std::strerror(errno), errno);
}

auto Address(uint32_t host, uint16_t port) -> sockaddr_in {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(host);
    return address;
}

} // namespace

auto UdpSocket::Bind(UdpAddress address, uint16_t port) -> Result<UdpSocket> {
    const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return Unexpected(LastError());
    }
    UdpSocket created(fd);
    const auto local = Address(address == UdpAddress::Loopback ? INADDR_LOOPBACK : INADDR_ANY, port);
    if (bind(fd, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
        return Unexpected(LastError());
    }
    return created;
}

UdpSocket::UdpSocket(intptr_t handle) : m_handle(handle) {
}

UdpSocket::~UdpSocket() {
    if (m_handle != kInvalid) {
        close(static_cast<int>(m_handle));
    }
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : m_handle(std::exchange(other.m_handle, kInvalid)) {
}

auto UdpSocket::operator=(UdpSocket&& other) noexcept -> UdpSocket& {
    if (this != &other) {
        if (m_handle != kInvalid) {
            close(static_cast<int>(m_handle));
        }
        m_handle = std::exchange(other.m_handle, kInvalid);
    }
    return *this;
}

auto UdpSocket::ConnectLoopback(uint16_t port) -> Result<> {
    const auto remote = Address(INADDR_LOOPBACK, port);
    if (connect(static_cast<int>(m_handle), reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) != 0) {
        return Unexpected(LastError());
    }
    return {};
}

auto UdpSocket::LocalPort() const -> uint16_t {
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    if (getsockname(static_cast<int>(m_handle), reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return 0;
    }
    return ntohs(address.sin_port);
}

auto UdpSocket::SetReceiveTimeout(std::chrono::milliseconds timeout) -> Result<> {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(timeout - seconds);
    const timeval value{.tv_sec = seconds.count(), .tv_usec = micros.count()};
    if (setsockopt(static_cast<int>(m_handle), SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value)) != 0) {
        return Unexpected(LastError());
    }
    return {};
}

auto UdpSocket::Send(std::span<const std::byte> data) const -> bool {
    return send(static_cast<int>(m_handle), data.data(), data.size(), 0) >= 0;
}

auto UdpSocket::Receive(std::span<std::byte> buffer) const -> std::optional<size_t> {
    const auto received = recv(static_cast<int>(m_handle), buffer.data(), buffer.size(), 0);
    if (received <= 0) {
        return std::nullopt;
    }
    return static_cast<size_t>(received);
}
