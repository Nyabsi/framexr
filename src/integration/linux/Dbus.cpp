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

#include "Dbus.hpp"

#include <format>
#include <utility>

auto Dbus::ConnectSessionBus() -> Result<DBusConnection*> {
    DBusError error;
    dbus_error_init(&error);
    DBusConnection* connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    if (connection == nullptr) {
        auto message = std::format("{}: {}", error.name != nullptr ? error.name : "", error.message != nullptr ? error.message : "");
        dbus_error_free(&error);
        return Unexpected(std::move(message));
    }
    dbus_connection_set_exit_on_disconnect(connection, FALSE);
    return connection;
}

auto Dbus::ConnectAddress(const std::string& address) -> Result<DBusConnection*> {
    DBusError error;
    dbus_error_init(&error);
    DBusConnection* connection = dbus_connection_open_private(address.c_str(), &error);
    if (connection != nullptr && dbus_bus_register(connection, &error) == FALSE) {
        CloseConnection(connection);
        connection = nullptr;
    }
    if (connection == nullptr) {
        auto message = std::format("{}: {}", error.name != nullptr ? error.name : "", error.message != nullptr ? error.message : "");
        dbus_error_free(&error);
        return Unexpected(std::move(message));
    }
    dbus_connection_set_exit_on_disconnect(connection, FALSE);
    return connection;
}

auto Dbus::CloseConnection(DBusConnection* connection) -> void {
    if (connection != nullptr) {
        dbus_connection_close(connection);
        dbus_connection_unref(connection);
    }
}

auto Dbus::CallMethod(DBusConnection* connection, DBusMessage* message, int timeout_ms) -> Result<DBusMessage*> {
    DBusError error;
    dbus_error_init(&error);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, message, timeout_ms, &error);
    dbus_message_unref(message);
    if (reply == nullptr) {
        auto text = std::format("{}: {}", error.name != nullptr ? error.name : "", error.message != nullptr ? error.message : "");
        dbus_error_free(&error);
        return Unexpected(std::move(text));
    }
    return reply;
}
