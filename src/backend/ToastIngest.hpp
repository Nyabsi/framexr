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

struct DBusMessage;

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
    std::mutex m_mutex;
    std::vector<std::vector<std::byte>> m_messages;
};
