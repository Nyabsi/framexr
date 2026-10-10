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
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

class AppState;

enum class ToastTopic : uint8_t {
    System,
    DesktopNotification,
    XsNotification,
    IpdChange,
};

enum class ToastDisplay : uint8_t {
    Hide,
    Center,
    Watch,
};

auto from_json(const nlohmann::json& json, ToastTopic& value) -> void;
auto to_json(nlohmann::json& json, const ToastTopic& value) -> void;
auto from_json(const nlohmann::json& json, ToastDisplay& value) -> void;
auto to_json(nlohmann::json& json, const ToastDisplay& value) -> void;

struct Toast {
    ToastTopic topic = ToastTopic::System;
    std::string title;
    std::string body;
    float opacity = 1.0f;
    float timeout = 3.0f;
    bool sound = false;

    auto Submit(AppState& app) const -> void;
    auto SubmitAt(AppState& app, std::chrono::steady_clock::time_point instant) const -> void;

    static auto ShowError(AppState& app, std::string_view message) -> void;
};
