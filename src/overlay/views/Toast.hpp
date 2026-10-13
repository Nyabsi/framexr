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

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "Config.hpp"
#include "View.hpp"

class Compose;
class View;

struct ToastMessage {
    ToastTopic topic = ToastTopic::System;
    std::string title;
    std::string body;
    float opacity = 1.0f;
    float timeout = 3.0f;
    bool sound = false;
};

class Toast final : public View {
  public:
    static auto Show(const ToastMessage& toast) -> void;
    static auto ShowAt(const ToastMessage& toast, std::chrono::steady_clock::time_point instant) -> void;
    static auto ShowError(std::string_view message) -> void;

  private:
    Toast() = default;

    static auto Create(const ToastMessage& toast) -> std::unique_ptr<Toast>;
};
