// FrameXR
//
// This is a port of wlx-capture from Rust to C++.
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
// Copyright (C) 2023-2025 galister and wlx-capture contributors
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

#include "ScreenCastPortal.hpp"

#include <algorithm>
#include <atomic>
#include <format>
#include <memory>
#include <mutex>
#include <random>
#include <string_view>

#include <dbus/dbus.h>

#include "Logger.hpp"

namespace {

constexpr auto kPortalBus = "org.freedesktop.portal.Desktop";
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kScreenCastInterface = "org.freedesktop.portal.ScreenCast";
constexpr auto kRequestInterface = "org.freedesktop.portal.Request";

auto MakeMessagePtr(DBusMessage* message) -> std::unique_ptr<DBusMessage, decltype(&dbus_message_unref)> {
    return {message, &dbus_message_unref};
}

auto TakeError(DBusError& error) -> std::string {
    std::string message = error.message != nullptr ? error.message : "unknown D-Bus error";
    dbus_error_free(&error);
    return message;
}

auto AppendEntry(DBusMessageIter& dictionary, const char* key, int type, const char* signature, const void* value) -> void {
    DBusMessageIter entry;
    DBusMessageIter variant;
    dbus_message_iter_open_container(&dictionary, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, static_cast<const void*>(&key));
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, signature, &variant);
    dbus_message_iter_append_basic(&variant, type, value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(&dictionary, &entry);
}

auto AppendString(DBusMessageIter& dictionary, const char* key, const std::string& value) -> void {
    const char* text = value.c_str();
    AppendEntry(dictionary, key, DBUS_TYPE_STRING, DBUS_TYPE_STRING_AS_STRING, static_cast<const void*>(&text));
}

auto AppendUint32(DBusMessageIter& dictionary, const char* key, uint32_t value) -> void {
    AppendEntry(dictionary, key, DBUS_TYPE_UINT32, DBUS_TYPE_UINT32_AS_STRING, &value);
}

auto AppendBool(DBusMessageIter& dictionary, const char* key, bool value) -> void {
    const dbus_bool_t flag = value ? TRUE : FALSE;
    AppendEntry(dictionary, key, DBUS_TYPE_BOOLEAN, DBUS_TYPE_BOOLEAN_AS_STRING, &flag);
}

auto ReadString(DBusMessageIter& iter) -> std::optional<std::string> {
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING
        && dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_OBJECT_PATH) {
        return std::nullopt;
    }
    const char* value = nullptr;
    dbus_message_iter_get_basic(&iter, static_cast<void*>(&value));
    return std::string(value);
}

auto ReadIntPair(DBusMessageIter& variant) -> std::optional<Eigen::Vector2i> {
    if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_STRUCT) {
        return std::nullopt;
    }
    DBusMessageIter fields;
    dbus_message_iter_recurse(&variant, &fields);
    Eigen::Vector2i pair = Eigen::Vector2i::Zero();
    for (int index = 0; index < 2; ++index) {
        if (dbus_message_iter_get_arg_type(&fields) != DBUS_TYPE_INT32) {
            return std::nullopt;
        }
        dbus_int32_t value = 0;
        dbus_message_iter_get_basic(&fields, &value);
        pair[index] = value;
        dbus_message_iter_next(&fields);
    }
    return pair;
}

auto ReadStreams(DBusMessageIter& variant) -> std::vector<PipewireStream> {
    std::vector<PipewireStream> streams;
    if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_ARRAY) {
        return streams;
    }
    DBusMessageIter array;
    dbus_message_iter_recurse(&variant, &array);
    for (; dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT; dbus_message_iter_next(&array)) {
        DBusMessageIter fields;
        dbus_message_iter_recurse(&array, &fields);
        PipewireStream stream;
        dbus_message_iter_get_basic(&fields, &stream.node_id);
        dbus_message_iter_next(&fields);

        DBusMessageIter properties;
        dbus_message_iter_recurse(&fields, &properties);
        for (; dbus_message_iter_get_arg_type(&properties) == DBUS_TYPE_DICT_ENTRY; dbus_message_iter_next(&properties)) {
            DBusMessageIter entry;
            dbus_message_iter_recurse(&properties, &entry);
            const auto key = ReadString(entry).value_or("");
            dbus_message_iter_next(&entry);
            DBusMessageIter value;
            dbus_message_iter_recurse(&entry, &value);
            if (key == "position") {
                stream.position = ReadIntPair(value);
            } else if (key == "size") {
                stream.size = ReadIntPair(value);
            }
        }
        streams.push_back(stream);
    }
    return streams;
}

auto CursorModeName(PortalCursorMode mode) -> std::string_view {
    switch (mode) {
    case PortalCursorMode::Hidden: return "Hidden";
    case PortalCursorMode::Embedded: return "Embedded";
    case PortalCursorMode::Metadata: return "Metadata";
    }
    return "Hidden";
}

} // namespace

ScreenCastPortal::ScreenCastPortal(DBusConnection* connection) : connection_(connection) {
    std::string unique_name = dbus_bus_get_unique_name(connection_);
    if (unique_name.starts_with(':')) {
        unique_name.erase(0, 1);
    }
    std::ranges::replace(unique_name, '.', '_');
    sender_ = unique_name;
}

auto ScreenCastPortal::SelectScreen(ScreenCastOptions options) -> std::future<Result<ScreenSelection>> {
    return std::async(std::launch::async, [options = std::move(options)]() -> Result<ScreenSelection> {
        static std::mutex mutex;
        std::scoped_lock lock(mutex);
        ASSIGN_OR_RETURN(auto* connection, Connection());
        ScreenCastPortal portal(connection);
        return portal.Run(options);
    });
}

auto ScreenCastPortal::Connection() -> Result<DBusConnection*> {
    static DBusConnection* connection = nullptr;
    if (connection != nullptr) {
        return connection;
    }
    dbus_threads_init_default();
    DBusError error;
    dbus_error_init(&error);
    connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    if (connection == nullptr) {
        return Unexpected(std::format("Failed to connect to the session bus: {}", TakeError(error)));
    }
    dbus_connection_set_exit_on_disconnect(connection, FALSE);
    return connection;
}

auto ScreenCastPortal::NewToken() -> std::string {
    static std::mt19937_64 generator{std::random_device{}()};
    return std::format("framexr{}", generator() % 10'000'000'000ULL);
}

auto ScreenCastPortal::Run(const ScreenCastOptions& options) -> Result<ScreenSelection> {
    static std::atomic<uint32_t> cached_cursor_modes{0};
    auto cursor_modes = cached_cursor_modes.load();
    if (cursor_modes == 0) {
        ASSIGN_OR_RETURN(cursor_modes, AvailableCursorModes());
        LOG_DEBUG("Available cursor modes: {:#x}", cursor_modes);
        cached_cursor_modes.store(cursor_modes);
    }

    auto cursor_mode = PortalCursorMode::Hidden;
    if (options.embed_mouse && (cursor_modes & static_cast<uint32_t>(PortalCursorMode::Embedded)) != 0) {
        cursor_mode = PortalCursorMode::Embedded;
    } else if ((cursor_modes & static_cast<uint32_t>(PortalCursorMode::Metadata)) != 0) {
        cursor_mode = PortalCursorMode::Metadata;
    }
    LOG_DEBUG("Selected cursor mode: {}", CursorModeName(cursor_mode));

    const uint32_t source_types = options.screens_only ? 1U : 7U;
    const uint32_t persist_mode = options.persist ? 2U : 0U;
    const auto session_token = NewToken();

    std::optional<std::string> session_handle;
    RETURN_IF_ERROR(Request(
        "CreateSession",
        std::nullopt,
        false,
        [&](DBusMessageIter& dictionary) -> void { AppendString(dictionary, "session_handle_token", session_token); },
        [&](const std::string& key, DBusMessageIter& variant) -> void {
            if (key == "session_handle") {
                session_handle = ReadString(variant);
            }
        }
    ));
    if (!session_handle) {
        session_handle = std::format("/org/freedesktop/portal/desktop/session/{}/{}", sender_, session_token);
    }

    RETURN_IF_ERROR(Request(
        "SelectSources",
        session_handle,
        false,
        [&](DBusMessageIter& dictionary) -> void {
            AppendUint32(dictionary, "types", source_types);
            AppendBool(dictionary, "multiple", options.multiple);
            AppendUint32(dictionary, "cursor_mode", static_cast<uint32_t>(cursor_mode));
            if (options.restore_token) {
                AppendString(dictionary, "restore_token", *options.restore_token);
            }
            AppendUint32(dictionary, "persist_mode", persist_mode);
        },
        [](const std::string&, DBusMessageIter&) -> void {}
    ));

    ScreenSelection selection;
    RETURN_IF_ERROR(Request(
        "Start",
        session_handle,
        true,
        [](DBusMessageIter&) -> void {},
        [&](const std::string& key, DBusMessageIter& variant) -> void {
            if (key == "streams") {
                selection.streams = ReadStreams(variant);
            } else if (key == "restore_token") {
                selection.restore_token = ReadString(variant);
            }
        }
    ));

    if (selection.streams.empty()) {
        return Unexpected("Portal request returned no streams");
    }
    return selection;
}

auto ScreenCastPortal::AvailableCursorModes() -> Result<uint32_t> {
    auto message = MakeMessagePtr(dbus_message_new_method_call(kPortalBus, kPortalPath, "org.freedesktop.DBus.Properties", "Get"));
    const char* interface = kScreenCastInterface;
    const char* property = "AvailableCursorModes";
    dbus_message_append_args(message.get(), DBUS_TYPE_STRING, &interface, DBUS_TYPE_STRING, &property, DBUS_TYPE_INVALID);

    DBusError error;
    dbus_error_init(&error);
    auto reply = MakeMessagePtr(
        dbus_connection_send_with_reply_and_block(connection_, message.get(), DBUS_TIMEOUT_USE_DEFAULT, &error)
    );
    if (!reply) {
        return Unexpected(std::format("Failed to query AvailableCursorModes: {}", TakeError(error)));
    }

    DBusMessageIter iter;
    DBusMessageIter variant;
    if (!dbus_message_iter_init(reply.get(), &iter) || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT) {
        return Unexpected("Unexpected AvailableCursorModes reply");
    }
    dbus_message_iter_recurse(&iter, &variant);
    if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_UINT32) {
        return Unexpected("Unexpected AvailableCursorModes type");
    }
    uint32_t modes = 0;
    dbus_message_iter_get_basic(&variant, &modes);
    return modes;
}

auto ScreenCastPortal::Request(
    const std::string& method,
    const std::optional<std::string>& session_handle,
    bool with_parent_window,
    const PortalOptionWriter& write_options,
    const PortalResultVisitor& visit_results
) -> Result<> {
    const auto token = NewToken();
    auto request_path = std::format("/org/freedesktop/portal/desktop/request/{}/{}", sender_, token);
    const auto match_rule = std::format("type='signal',interface='{}',member='Response',path='{}'", kRequestInterface, request_path);

    DBusError error;
    dbus_error_init(&error);
    dbus_bus_add_match(connection_, match_rule.c_str(), &error);
    if (dbus_error_is_set(&error)) {
        return Unexpected(std::format("Failed to add D-Bus match: {}", TakeError(error)));
    }

    auto message = MakeMessagePtr(dbus_message_new_method_call(kPortalBus, kPortalPath, kScreenCastInterface, method.c_str()));
    DBusMessageIter args;
    dbus_message_iter_init_append(message.get(), &args);
    if (session_handle) {
        const char* path = session_handle->c_str();
        dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, static_cast<const void*>(&path));
    }
    if (with_parent_window) {
        const char* parent_window = "";
        dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, static_cast<const void*>(&parent_window));
    }
    DBusMessageIter dictionary;
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dictionary);
    AppendString(dictionary, "handle_token", token);
    write_options(dictionary);
    dbus_message_iter_close_container(&args, &dictionary);

    auto reply = MakeMessagePtr(
        dbus_connection_send_with_reply_and_block(connection_, message.get(), DBUS_TIMEOUT_USE_DEFAULT, &error)
    );
    if (!reply) {
        dbus_bus_remove_match(connection_, match_rule.c_str(), nullptr);
        return Unexpected(std::format("Portal {} failed: {}", method, TakeError(error)));
    }

    std::string extra_match;
    DBusMessageIter reply_iter;
    if (dbus_message_iter_init(reply.get(), &reply_iter)) {
        if (auto handle = ReadString(reply_iter); handle && *handle != request_path) {
            request_path = *handle;
            extra_match = std::format("type='signal',interface='{}',member='Response',path='{}'", kRequestInterface, request_path);
            dbus_bus_add_match(connection_, extra_match.c_str(), nullptr);
        }
    }

    auto result = WaitForResponse(request_path, visit_results);
    dbus_bus_remove_match(connection_, match_rule.c_str(), nullptr);
    if (!extra_match.empty()) {
        dbus_bus_remove_match(connection_, extra_match.c_str(), nullptr);
    }
    return result;
}

auto ScreenCastPortal::WaitForResponse(const std::string& request_path, const PortalResultVisitor& visit_results) -> Result<> {
    while (true) {
        while (auto message = MakeMessagePtr(dbus_connection_pop_message(connection_))) {
            if (!dbus_message_is_signal(message.get(), kRequestInterface, "Response")) {
                continue;
            }
            const char* path = dbus_message_get_path(message.get());
            if (path == nullptr || request_path != path) {
                continue;
            }

            DBusMessageIter iter;
            if (!dbus_message_iter_init(message.get(), &iter) || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UINT32) {
                return Unexpected("Malformed portal response");
            }
            uint32_t response = 0;
            dbus_message_iter_get_basic(&iter, &response);
            if (response != 0) {
                return Unexpected(std::format("Portal request didn't succeed: {}", response == 1 ? "Cancelled" : "Other"));
            }

            dbus_message_iter_next(&iter);
            if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
                DBusMessageIter results;
                dbus_message_iter_recurse(&iter, &results);
                for (; dbus_message_iter_get_arg_type(&results) == DBUS_TYPE_DICT_ENTRY; dbus_message_iter_next(&results)) {
                    DBusMessageIter entry;
                    dbus_message_iter_recurse(&results, &entry);
                    const auto key = ReadString(entry).value_or("");
                    dbus_message_iter_next(&entry);
                    DBusMessageIter variant;
                    dbus_message_iter_recurse(&entry, &variant);
                    visit_results(key, variant);
                }
            }
            return {};
        }
        if (!dbus_connection_read_write(connection_, 1000)) {
            return Unexpected("D-Bus connection closed while waiting for portal response");
        }
    }
}
