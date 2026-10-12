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

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <dbus/dbus.h>

#include "Dbus.hpp"
#include "Logger.hpp"

namespace {

constexpr std::string_view kNativeSocketName = "framexr.toast";

constexpr int kMonitorTimeoutMs = 5000;

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

auto NotifyFilter(DBusConnection*, DBusMessage* message, void* data) -> DBusHandlerResult {
    if (dbus_message_is_method_call(message, Dbus::kNotificationsInterface, "Notify") == FALSE) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    auto& ingest = *static_cast<ToastIngest*>(data);
    if (auto toast = ToastIngest::FromDbusNotify(message)) {
        ingest.Push(*toast);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

} // namespace

auto NotificationManager::RunDesktop() -> void {
    auto connection = Dbus::ConnectSessionBus();
    if (!connection) {
        LOG_ERROR("Failed to connect to dbus. Desktop notifications will not work. Cause: {}", connection.error().Message());
        return;
    }
    m_dbus = *connection;

    const std::string rule = std::format(
        "type='method_call',path='{}',interface='{}',member='Notify',eavesdrop='true'",
        Dbus::kNotificationsPath,
        Dbus::kNotificationsInterface
    );

    dbus_connection_add_filter(m_dbus, NotifyFilter, m_ingest.get(), nullptr);

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

    if (auto reply = Dbus::CallMethod(m_dbus, become_monitor, kMonitorTimeoutMs)) {
        dbus_message_unref(*reply);
        LOG_INFO("Listening to DBus notifications via BecomeMonitor.");
        return;
    }

    DBusError error;
    dbus_error_init(&error);
    dbus_bus_add_match(m_dbus, rule.c_str(), &error);
    if (dbus_error_is_set(&error) == TRUE) {
        dbus_error_free(&error);
        LOG_ERROR("Failed to add DBus match. Desktop notifications will not work.");
    } else {
        LOG_INFO("Listening to DBus notifications via eavesdrop.");
    }
}

auto NotificationManager::PollDesktop() -> void {
    if (m_dbus != nullptr) {
        dbus_connection_read_write(m_dbus, 0);
        while (dbus_connection_dispatch(m_dbus) == DBUS_DISPATCH_DATA_REMAINS) {
        }
    }
}

auto NotificationManager::CloseDesktop() -> void {
    if (m_dbus != nullptr) {
        dbus_connection_remove_filter(m_dbus, NotifyFilter, m_ingest.get());
        Dbus::CloseConnection(m_dbus);
        m_dbus = nullptr;
    }
}

auto NotificationManager::RunNative() -> void {
    m_native_thread = std::thread([ingest = m_ingest, running = m_running] -> void {
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
