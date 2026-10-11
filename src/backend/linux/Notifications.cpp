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

#include "Notifications.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <utility>

#include <dbus/dbus.h>

#include "Logger.hpp"

namespace {

constexpr std::string_view kNativeSocketName = "framexr.toast";
constexpr int kDbusTimeoutMs = 1000;
constexpr int kMonitorTimeoutMs = 5000;
constexpr char kNotificationsInterface[] = "org.freedesktop.Notifications";
constexpr char kNotificationsPath[] = "/org/freedesktop/Notifications";

auto NativeSocketPath() -> std::optional<std::filesystem::path> {
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (runtime_dir == nullptr || *runtime_dir == '\0') {
        return std::nullopt;
    }
    return std::filesystem::path(runtime_dir) / kNativeSocketName;
}

auto SetReceiveTimeout(int socket) -> bool {
    const timeval timeout{.tv_sec = 0, .tv_usec = 200'000};
    return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0;
}

auto ConnectSessionBus() -> Result<DBusConnection*> {
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

auto CloseConnection(DBusConnection* connection) -> void {
    if (connection != nullptr) {
        dbus_connection_close(connection);
        dbus_connection_unref(connection);
    }
}

auto CallMethod(DBusConnection* connection, DBusMessage* message, int timeout_ms) -> Result<DBusMessage*> {
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

auto NotifyFilter(DBusConnection*, DBusMessage* message, void* data) -> DBusHandlerResult {
    if (dbus_message_is_method_call(message, kNotificationsInterface, "Notify") == FALSE) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    auto& ingest = *static_cast<ToastIngest*>(data);
    if (auto toast = ToastIngest::FromDbusNotify(message)) {
        ingest.Push(*toast);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

auto AppendHint(DBusMessageIter& dictionary, const char* key, int type, const void* value) -> void {
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

auto ToastIngest::FromDbusNotify(DBusMessage* message) -> Result<Toast> {
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
        == FALSE) {
        auto text = std::string(error.message != nullptr ? error.message : "invalid Notify arguments");
        dbus_error_free(&error);
        return Unexpected(std::move(text));
    }

    return Toast{
        .topic = ToastTopic::DesktopNotification,
        .title = *summary == '\0' ? app_name : summary,
        .body = body,
        .opacity = 1.0f,
        .timeout = 5.0f,
    };
}

auto NotificationManager::RunDesktop() -> void {
    auto connection = ConnectSessionBus();
    if (!connection) {
        LOG_ERROR("Failed to connect to dbus. Desktop notifications will not work. Cause: {}", connection.error().Message());
        return;
    }
    dbus_ = *connection;

    const std::string rule = std::format(
        "type='method_call',path='{}',interface='{}',member='Notify',eavesdrop='true'",
        kNotificationsPath,
        kNotificationsInterface
    );

    dbus_connection_add_filter(dbus_, NotifyFilter, ingest_.get(), nullptr);

    DBusMessage* become_monitor = dbus_message_new_method_call(
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus.Monitoring",
        "BecomeMonitor"
    );
    {
        DBusMessageIter args;
        DBusMessageIter rules;
        const char* rule_text = rule.c_str();
        const dbus_uint32_t flags = 0;
        dbus_message_iter_init_append(become_monitor, &args);
        dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING, &rules);
        dbus_message_iter_append_basic(&rules, DBUS_TYPE_STRING, static_cast<const void*>(&rule_text));
        dbus_message_iter_close_container(&args, &rules);
        dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &flags);
    }

    if (auto reply = CallMethod(dbus_, become_monitor, kMonitorTimeoutMs)) {
        dbus_message_unref(*reply);
        LOG_INFO("Listening to DBus notifications via BecomeMonitor.");
        return;
    }

    DBusError error;
    dbus_error_init(&error);
    dbus_bus_add_match(dbus_, rule.c_str(), &error);
    if (dbus_error_is_set(&error) == TRUE) {
        dbus_error_free(&error);
        LOG_ERROR("Failed to add DBus match. Desktop notifications will not work.");
    } else {
        LOG_INFO("Listening to DBus notifications via eavesdrop.");
    }
}

auto NotificationManager::PollDesktop() -> void {
    if (dbus_ != nullptr) {
        dbus_connection_read_write(dbus_, 0);
        while (dbus_connection_dispatch(dbus_) == DBUS_DISPATCH_DATA_REMAINS) {
        }
    }
}

auto NotificationManager::CloseDesktop() -> void {
    if (dbus_ != nullptr) {
        dbus_connection_remove_filter(dbus_, NotifyFilter, ingest_.get());
        CloseConnection(dbus_);
        dbus_ = nullptr;
    }
}

auto NotificationManager::RunNative() -> void {
    native_thread_ = std::thread([ingest = ingest_, running = running_] -> void {
        const auto path = NativeSocketPath();
        if (!path) {
            LOG_WARN("XDG_RUNTIME_DIR is not set, native toast endpoint disabled");
            return;
        }
        const int socket_fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        const auto path_text = path->string();
        if (socket_fd < 0 || path_text.size() >= sizeof(address.sun_path)) {
            LOG_ERROR("Failed to create native toast socket @ {}", path_text);
            if (socket_fd >= 0) {
                close(socket_fd);
            }
            return;
        }
        std::ranges::copy(path_text, std::begin(address.sun_path));
        unlink(path_text.c_str());
        if (bind(socket_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            LOG_ERROR("Failed to bind native toast socket @ {}: {}", path_text, std::strerror(errno));
            close(socket_fd);
            return;
        }
        SetReceiveTimeout(socket_fd);

        std::vector<std::byte> buffer(size_t{64} * 1024);
        while (running->load(std::memory_order_relaxed)) {
            const auto received = recv(socket_fd, buffer.data(), buffer.size(), 0);
            if (received <= 0) {
                continue;
            }
            auto toast = ToastIngest::Decode(std::span(buffer.data(), static_cast<size_t>(received)));
            if (!toast) {
                LOG_ERROR("Failed to parse native toast message: {}", toast.error().Message());
                continue;
            }
            ingest->Push(*toast);
        }
        close(socket_fd);
        unlink(path_text.c_str());
    });
}

DbusNotificationSender::DbusNotificationSender(DBusConnection* connection) : connection_(connection) {
}

DbusNotificationSender::~DbusNotificationSender() {
    CloseConnection(connection_);
}

auto DbusNotificationSender::Create() -> Result<std::unique_ptr<DbusNotificationSender>> {
    ASSIGN_OR_RETURN(auto* connection, ConnectSessionBus());
    return std::unique_ptr<DbusNotificationSender>(new DbusNotificationSender(connection));
}

auto DbusNotificationSender::NotifySend(
    std::string_view summary,
    std::string_view body,
    uint8_t urgency,
    int32_t timeout,
    uint32_t replaces_id,
    bool transient
) -> Result<uint32_t> {
    DBusMessage*
        message = dbus_message_new_method_call(kNotificationsInterface, kNotificationsPath, kNotificationsInterface, "Notify");
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

    ASSIGN_OR_RETURN(auto* reply, CallMethod(connection_, message, kDbusTimeoutMs));
    dbus_uint32_t id = 0;
    DBusError error;
    dbus_error_init(&error);
    const bool parsed = dbus_message_get_args(reply, &error, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID) == TRUE;
    dbus_message_unref(reply);
    if (!parsed) {
        auto text = std::string(error.message != nullptr ? error.message : "invalid Notify reply");
        dbus_error_free(&error);
        return Unexpected(std::move(text));
    }
    return id;
}

auto DbusNotificationSender::NotifyClose(uint32_t id) -> Result<> {
    DBusMessage* message = dbus_message_new_method_call(
        kNotificationsInterface,
        kNotificationsPath,
        kNotificationsInterface,
        "CloseNotification"
    );
    const dbus_uint32_t notification = id;
    dbus_message_append_args(message, DBUS_TYPE_UINT32, &notification, DBUS_TYPE_INVALID);
    ASSIGN_OR_RETURN(auto* reply, CallMethod(connection_, message, kDbusTimeoutMs));
    dbus_message_unref(reply);
    return {};
}
