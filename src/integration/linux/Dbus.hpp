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

#include <string>

#include <dbus/dbus.h>

#include "Error.hpp"

class Dbus final {
  public:
    static constexpr char kNotificationsInterface[] = "org.freedesktop.Notifications";
    static constexpr char kNotificationsPath[] = "/org/freedesktop/Notifications";

    static auto ConnectSessionBus() -> Result<DBusConnection*>;
    static auto ConnectAddress(const std::string& address) -> Result<DBusConnection*>;
    static auto CloseConnection(DBusConnection* connection) -> void;
    static auto CallMethod(DBusConnection* connection, DBusMessage* message, int timeout_ms) -> Result<DBusMessage*>;
};
