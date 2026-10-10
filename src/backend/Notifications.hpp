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

class AppState;
struct DBusConnection;
struct DBusMessage;

class ByteWriter final {
  public:
    auto Byte(std::byte value) -> void;
    auto Float(float value) -> void;
    auto String(std::string_view value) -> void;
    auto Take() -> std::vector<std::byte>;

  private:
    std::vector<std::byte> bytes_;
};

class ByteReader final {
  public:
    explicit ByteReader(std::span<const std::byte> bytes);

    auto Byte() -> std::optional<std::byte>;
    auto Float() -> std::optional<float>;
    auto String() -> std::optional<std::string>;
    [[nodiscard]] auto Empty() const -> bool;

  private:
    std::span<const std::byte> bytes_;
};

class ToastIngest final {
  public:
    static auto Encode(const Toast& toast) -> std::vector<std::byte>;
    static auto Decode(std::span<const std::byte> message) -> Result<Toast>;
    static auto FromXsOverlay(std::string_view json) -> Result<std::optional<Toast>>;
    static auto Utf8Error(std::string_view text) -> std::optional<std::string>;
    static auto FromDbusNotify(DBusMessage* message) -> Result<Toast>;

    auto Push(const Toast& toast) -> void;
    auto Drain() -> std::vector<Toast>;

  private:
    std::mutex mutex_;
    std::vector<std::vector<std::byte>> messages_;
};

class NotificationManager final {
  public:
    NotificationManager();
    ~NotificationManager();
    NotificationManager(const NotificationManager&) = delete;
    auto operator=(const NotificationManager&) -> NotificationManager& = delete;

    auto RunDbus() -> void;
    auto RunUdp() -> void;
    auto SubmitPending(AppState& app) -> void;

  private:
    std::shared_ptr<ToastIngest> ingest_;
    std::shared_ptr<std::atomic<bool>> running_;
    DBusConnection* dbus_ = nullptr;
    std::thread udp_thread_;
    std::thread native_thread_;
};

class DbusNotificationSender final {
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

    DBusConnection* connection_;
};
