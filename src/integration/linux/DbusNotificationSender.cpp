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

#include "DbusNotificationSender.hpp"

#include <array>
#include <utility>

#include <dbus/dbus.h>

#include "Dbus.hpp"

namespace
{

constexpr int kDbusTimeoutMs = 1000;

auto AppendHint(DBusMessageIter& dictionary, const char* key, int type, const void* value) -> void
{
    DBusMessageIter entry;
    DBusMessageIter variant;
    const std::array<char, 2> signature{static_cast<char>(type), '\0'};
    dbus_message_iter_open_container(&dictionary, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, static_cast<const void*>(&key));
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, signature.data(), &variant);
    dbus_message_iter_append_basic(&variant, type, value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(&dictionary, &entry);
}

} // namespace

DbusNotificationSender::DbusNotificationSender(DBusConnection* connection) : m_connection(connection)
{
}

DbusNotificationSender::~DbusNotificationSender()
{
    Dbus::CloseConnection(m_connection);
}

auto DbusNotificationSender::Create() -> Result<std::unique_ptr<DbusNotificationSender>>
{
    ASSIGN_OR_RETURN(auto* connection, Dbus::ConnectSessionBus());
    return std::unique_ptr<DbusNotificationSender>(new DbusNotificationSender(connection));
}

auto DbusNotificationSender::NotifySend(
    std::string_view summary,
    std::string_view body,
    uint8_t urgency,
    int32_t timeout,
    uint32_t replaces_id,
    bool transient
) -> Result<uint32_t>
{
    DBusMessage* message = dbus_message_new_method_call(
        Dbus::kNotificationsInterface,
        Dbus::kNotificationsPath,
        Dbus::kNotificationsInterface,
        "Notify"
    );
    const std::string summary_text(summary);
    const std::string body_text(body);
    const char* app_name = "FrameXR";
    const char* app_icon = "";
    const char* summary_ptr = summary_text.c_str();
    const char* body_ptr = body_text.c_str();
    const dbus_uint32_t replaces = replaces_id;
    const dbus_int32_t expire = timeout;
    const unsigned char urgency_value = urgency;
    const dbus_bool_t transient_value = transient ? TRUE : FALSE;

    DBusMessageIter args;
    DBusMessageIter actions;
    DBusMessageIter hints;
    dbus_message_iter_init_append(message, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, static_cast<const void*>(&app_name));
    dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &replaces);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, static_cast<const void*>(&app_icon));
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, static_cast<const void*>(&summary_ptr));
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, static_cast<const void*>(&body_ptr));
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING, &actions);
    dbus_message_iter_close_container(&args, &actions);
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &hints);
    AppendHint(hints, "urgency", DBUS_TYPE_BYTE, &urgency_value);
    AppendHint(hints, "transient", DBUS_TYPE_BOOLEAN, &transient_value);
    dbus_message_iter_close_container(&args, &hints);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &expire);

    ASSIGN_OR_RETURN(auto* reply, Dbus::CallMethod(m_connection, message, kDbusTimeoutMs));
    dbus_uint32_t id = 0;
    DBusError error;
    dbus_error_init(&error);
    const bool parsed = dbus_message_get_args(reply, &error, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID) == TRUE;
    dbus_message_unref(reply);
    if (!parsed)
    {
        auto text = std::string(error.message != nullptr ? error.message : "invalid Notify reply");
        dbus_error_free(&error);
        return Unexpected(std::move(text));
    }
    return id;
}

auto DbusNotificationSender::NotifyClose(uint32_t id) -> Result<>
{
    DBusMessage* message = dbus_message_new_method_call(
        Dbus::kNotificationsInterface,
        Dbus::kNotificationsPath,
        Dbus::kNotificationsInterface,
        "CloseNotification"
    );
    const dbus_uint32_t notification = id;
    dbus_message_append_args(message, DBUS_TYPE_UINT32, &notification, DBUS_TYPE_INVALID);
    ASSIGN_OR_RETURN(auto* reply, Dbus::CallMethod(m_connection, message, kDbusTimeoutMs));
    dbus_message_unref(reply);
    return {};
}
