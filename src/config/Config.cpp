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

#include "Config.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>
#include <vector>

#include "Logger.hpp"
#include "Platform.hpp"
#include "Yaml.hpp"

Config* g_config = nullptr;

namespace
{

constexpr auto kTopicNames = std::to_array<std::pair<ToastTopic, std::string_view>>({
    {ToastTopic::System, "System"},
    {ToastTopic::DesktopNotification, "DesktopNotification"},
    {ToastTopic::XsNotification, "XSNotification"},
    {ToastTopic::IpdChange, "IpdChange"},
});

constexpr auto kDisplayNames = std::to_array<std::pair<ToastDisplay, std::string_view>>({
    {ToastDisplay::Hide, "Hide"},
    {ToastDisplay::Center, "Center"},
    {ToastDisplay::Watch, "Watch"},
});

} // namespace

auto from_json(const nlohmann::json& json, Handedness& value) -> void
{
    const auto name = Json::VariantName(json);
    if (name == "Left")
    {
        value = Handedness::Left;
    }
    else if (name == "Right")
    {
        value = Handedness::Right;
    }
    else
    {
        throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}`, expected `Left` or `Right`", name), &json);
    }
}

auto to_json(nlohmann::json& json, const Handedness& value) -> void
{
    json = value == Handedness::Left ? "Left" : "Right";
}

auto nlohmann::adl_serializer<std::map<ToastTopic, ToastDisplay>>::from_json(
    const nlohmann::json& json,
    std::map<ToastTopic, ToastDisplay>& value
) -> void
{
    value.clear();
    for (const auto& [key, display] : json.items())
    {
        value[nlohmann::json(key).get<ToastTopic>()] = display.get<ToastDisplay>();
    }
}

auto nlohmann::adl_serializer<std::map<ToastTopic, ToastDisplay>>::to_json(
    nlohmann::json& json,
    const std::map<ToastTopic, ToastDisplay>& value
) -> void
{
    json = nlohmann::json::object();
    for (const auto& [topic, display] : value)
    {
        json[nlohmann::json(topic).get<std::string>()] = display;
    }
}

auto from_json(const nlohmann::json& json, ToastTopic& value) -> void
{
    const auto name = Json::VariantName(json);
    for (const auto& [topic, topic_name] : kTopicNames)
    {
        if (topic_name == name)
        {
            value = topic;
            return;
        }
    }
    throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for ToastTopic", name), &json);
}

auto to_json(nlohmann::json& json, const ToastTopic& value) -> void
{
    for (const auto& [topic, topic_name] : kTopicNames)
    {
        if (topic == value)
        {
            json = topic_name;
        }
    }
}

auto from_json(const nlohmann::json& json, ToastDisplay& value) -> void
{
    const auto name = Json::VariantName(json);
    for (const auto& [display, display_name] : kDisplayNames)
    {
        if (display_name == name)
        {
            value = display;
            return;
        }
    }
    throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for DisplayMethod", name), &json);
}

auto to_json(nlohmann::json& json, const ToastDisplay& value) -> void
{
    for (const auto& [display, display_name] : kDisplayNames)
    {
        if (display == value)
        {
            json = display_name;
        }
    }
}
