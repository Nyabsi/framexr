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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Error.hpp"
#include "Toast.hpp"
#include "ToastIngest.hpp"

struct DBusConnection;

class NotificationManager final {
  public:
    NotificationManager();
    ~NotificationManager();
    NotificationManager(const NotificationManager&) = delete;
    auto operator=(const NotificationManager&) -> NotificationManager& = delete;

    auto RunDesktop() -> void;
    auto RunUdp() -> void;
    auto SubmitPending() -> void;

  private:
    auto PollDesktop() -> void;
    auto CloseDesktop() -> void;
    auto RunNative() -> void;

    std::shared_ptr<ToastIngest> m_ingest;
    std::shared_ptr<std::atomic<bool>> m_running;
    DBusConnection* m_dbus = nullptr;
    std::thread m_udp_thread;
    std::thread m_native_thread;
};
