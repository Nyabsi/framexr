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

#include "Notifications.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <format>

#include "Config.hpp"
#include "Logger.hpp"
#include "UdpSocket.hpp"
#include "Toast.hpp"

namespace {

constexpr uint16_t kXsOverlayPort = 42069;

constexpr std::chrono::milliseconds kReceiveTimeout{200};

} // namespace

Notifications::Notifications() : m_ingest(std::make_shared<ToastIngest>()), m_running(std::make_shared<std::atomic<bool>>(true)) {
}

Notifications::~Notifications() {
    m_running->store(false, std::memory_order_relaxed);
    if (m_udp_thread.joinable()) {
        m_udp_thread.join();
    }
    if (m_native_thread.joinable()) {
        m_native_thread.join();
    }
    CloseDesktop();
}

auto Notifications::SubmitPending() -> void {
    PollDesktop();

    auto toasts = m_ingest->Drain();
    if (!g_config->notifications_enabled) {
        return;
    }
    for (const auto& toast : toasts) {
        Toast::Show(toast);
    }
}

auto Notifications::RunUdp() -> void {
    m_udp_thread = std::thread([ingest = m_ingest, running = m_running] -> void {
        auto socket = UdpSocket::Bind(UdpAddress::Loopback, kXsOverlayPort);
        if (!socket) {
            LOG_ERROR("Failed to bind notification socket @ 127.0.0.1:{}: {}", kXsOverlayPort, socket.error().Message());
            return;
        }
        if (const auto result = socket->SetReceiveTimeout(kReceiveTimeout); !result) {
            LOG_ERROR("Failed to set read timeout: {}", result.error().Message());
        }

        std::vector<std::byte> buffer(size_t{16} * 1024);
        while (running->load(std::memory_order_relaxed)) {
            const auto received = socket->Receive(buffer);
            if (!received) {
                continue;
            }
            const std::string_view payload(reinterpret_cast<const char*>(buffer.data()), *received);
            if (const auto utf8_error = ToastIngest::Utf8Error(payload)) {
                LOG_ERROR("Failed to receive notification message: {}", *utf8_error);
                continue;
            }
            auto toast = ToastIngest::FromXsOverlay(payload);
            if (!toast) {
                LOG_ERROR("Failed to parse notification message: {}", toast.error().Message());
                continue;
            }
            if (*toast) {
                ingest->Push(**toast);
            }
        }
        LOG_INFO("Notification listener stopped.");
    });

    RunNative();
}
