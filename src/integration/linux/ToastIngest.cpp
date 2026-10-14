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

#include "ToastIngest.hpp"

#include <utility>

#include <dbus/dbus.h>

#include "Error.hpp"
#include "Toast.hpp"

auto ToastIngest::FromDbusNotify(DBusMessage* message) -> Result<ToastMessage>
{
    const char* app_name = nullptr;
    dbus_uint32_t replaces_id = 0;
    const char* app_icon = nullptr;
    const char* summary = nullptr;
    const char* body = nullptr;
    DBusError error;
    dbus_error_init(&error);
    if (dbus_message_get_args(
            message,
            &error,
            DBUS_TYPE_STRING,
            &app_name,
            DBUS_TYPE_UINT32,
            &replaces_id,
            DBUS_TYPE_STRING,
            &app_icon,
            DBUS_TYPE_STRING,
            &summary,
            DBUS_TYPE_STRING,
            &body,
            DBUS_TYPE_INVALID
        )
        == FALSE)
    {
        auto text = std::string(error.message != nullptr ? error.message : "invalid Notify arguments");
        dbus_error_free(&error);
        return Unexpected(std::move(text));
    }

    return ToastMessage{
        .topic = ToastTopic::DesktopNotification,
        .title = *summary == '\0' ? app_name : summary,
        .body = body,
        .opacity = 1.0f,
        .timeout = 5.0f,
    };
}
