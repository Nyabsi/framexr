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

#include <winsock2.h>

#include <format>
#include <mutex>
#include <system_error>
#include <utility>

namespace {

constexpr auto kInvalid = static_cast<intptr_t>(INVALID_SOCKET);

auto EnsureWinsock() -> void {
    static std::once_flag once;
    std::call_once(once, []() -> void {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
    });
}

auto LastError() -> std::string {
    const int error = WSAGetLastError();
    return std::format("{} (os error {})", std::system_category().message(error), error);
}

auto Address(uint32_t host, uint16_t port) -> sockaddr_in {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(host);
    return address;
}

auto Handle(intptr_t handle) -> SOCKET {
    return static_cast<SOCKET>(handle);
}

} // namespace

auto UdpSocket::Bind(UdpAddress address, uint16_t port) -> Result<UdpSocket> {
    EnsureWinsock();
    const SOCKET socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_handle == INVALID_SOCKET) {
        return Unexpected(LastError());
    }
    UdpSocket created(static_cast<intptr_t>(socket_handle));
    const auto local = Address(address == UdpAddress::Loopback ? INADDR_LOOPBACK : INADDR_ANY, port);
    if (bind(socket_handle, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
        return Unexpected(LastError());
    }
    return created;
}

UdpSocket::UdpSocket(intptr_t handle) : handle_(handle) {
}

UdpSocket::~UdpSocket() {
    if (handle_ != kInvalid) {
        closesocket(Handle(handle_));
    }
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : handle_(std::exchange(other.handle_, kInvalid)) {
}

auto UdpSocket::operator=(UdpSocket&& other) noexcept -> UdpSocket& {
    if (this != &other) {
        if (handle_ != kInvalid) {
            closesocket(Handle(handle_));
        }
        handle_ = std::exchange(other.handle_, kInvalid);
    }
    return *this;
}

auto UdpSocket::ConnectLoopback(uint16_t port) -> Result<> {
    const auto remote = Address(INADDR_LOOPBACK, port);
    if (connect(Handle(handle_), reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) != 0) {
        return Unexpected(LastError());
    }
    return {};
}

auto UdpSocket::LocalPort() const -> uint16_t {
    sockaddr_in address{};
    int length = sizeof(address);
    if (getsockname(Handle(handle_), reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return 0;
    }
    return ntohs(address.sin_port);
}

auto UdpSocket::SetReceiveTimeout(std::chrono::milliseconds timeout) -> Result<> {
    const auto value = static_cast<DWORD>(timeout.count());
    if (setsockopt(Handle(handle_), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&value), sizeof(value)) != 0) {
        return Unexpected(LastError());
    }
    return {};
}

auto UdpSocket::Send(std::span<const std::byte> data) const -> bool {
    return send(Handle(handle_), reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0) != SOCKET_ERROR;
}

auto UdpSocket::Receive(std::span<std::byte> buffer) const -> std::optional<size_t> {
    const int received = recv(Handle(handle_), reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
    if (received <= 0) {
        return std::nullopt;
    }
    return static_cast<size_t>(received);
}
