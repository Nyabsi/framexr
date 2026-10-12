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
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <Eigen/Core>
#include <nlohmann/json.hpp>

#include "Control.hpp"
#include "Process.hpp"

enum class NumberPadding : uint8_t {
    Zero,
    Space,
    None,
};

struct StaticLabelContent {
    std::string text;
};

struct ExecLabelContent {
    std::vector<std::string> command;
    float interval = 0.0f;
};

struct ClockLabelContent {
    std::string format;
    std::optional<std::variant<size_t, std::string>> timezone;
    std::optional<int> city_size;
};

struct TimezoneLabelContent {
    std::variant<size_t, std::string> timezone = size_t{0};
};

struct TimerLabelContent {
    std::string format;
};

struct BatteryLabelContent {
    size_t device = 0;
    float low_threshold = 0.0f;
    std::string low_color;
    std::string charging_color;
};

struct DragMultiplierLabelContent {};

struct IpdLabelContent {};

using LabelContent = std::variant<
    StaticLabelContent,
    ExecLabelContent,
    ClockLabelContent,
    TimezoneLabelContent,
    TimerLabelContent,
    BatteryLabelContent,
    DragMultiplierLabelContent,
    IpdLabelContent>;

auto from_json(const nlohmann::json& json, LabelContent& value) -> void;

auto to_json(nlohmann::ordered_json& json, const LabelContent& value) -> void;

struct LabelSetup {
    std::unique_ptr<Control> control;
    std::string text;
    std::optional<Eigen::Vector4f> color;
};

class Labels {
  public:
    static auto Create(const LabelContent& content, const Eigen::Vector4f& fg_color) -> LabelSetup;
    static auto FormatTime(std::string_view format, const std::chrono::time_zone* zone, std::chrono::system_clock::time_point now)
        -> std::string;
    static auto City(const std::optional<std::variant<size_t, std::string>>& zone) -> std::string;
};
