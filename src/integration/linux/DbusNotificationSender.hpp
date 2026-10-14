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

struct DBusConnection;

class DbusNotificationSender final
{
  public:
    static auto Create() -> Result<std::unique_ptr<DbusNotificationSender>>;

    ~DbusNotificationSender();
    DbusNotificationSender(const DbusNotificationSender&) = delete;
    auto operator=(const DbusNotificationSender&) -> DbusNotificationSender& = delete;

    auto NotifySend(
        std::string_view summary,
        std::string_view body,
        uint8_t urgency,
        int32_t timeout,
        uint32_t replaces_id,
        bool transient
    ) -> Result<uint32_t>;
    auto NotifyClose(uint32_t id) -> Result<>;

  private:
    explicit DbusNotificationSender(DBusConnection* connection);

    DBusConnection* m_connection;
};
