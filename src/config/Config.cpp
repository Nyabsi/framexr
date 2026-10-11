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
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>
#include <vector>

#include "Logger.hpp"
#include "Yaml.hpp"

namespace {

constexpr std::string_view kFallbackRoot = "/tmp/framexr";

auto ResolveRoot() -> std::filesystem::path {
    if (const char* config_home = std::getenv("XDG_CONFIG_HOME"); config_home != nullptr && *config_home == '/') {
        return std::filesystem::path(config_home) / "framexr";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config" / "framexr";
    }
    LOG_ERROR("Err: Failed to find config path, using {}", kFallbackRoot);
    return std::filesystem::path(kFallbackRoot);
}

auto SanitizeRange(std::string_view name, float value, float from, float to) -> void {
    if (!std::isnormal(value) || value < from || value > to) {
        FatalError(std::format("Config: {} needs to be between {} and {}", name, from, to));
    }
}

auto WriteJson(const std::filesystem::path& path, const nlohmann::json& value) -> Result<> {
    std::ofstream stream(path, std::ios::trunc);
    if (!stream) {
        return Unexpected(std::format("Failed to write {}", path.string()));
    }
    stream << value.dump(2);
    return {};
}

auto ToFloatPairs(const std::vector<std::pair<std::string, float>>& values) -> nlohmann::json {
    auto array = nlohmann::json::array();
    for (const auto& [name, value] : values) {
        array.push_back({name, Json::ShortestFloat(value)});
    }
    return array;
}

} // namespace

auto Config::Root() -> const std::filesystem::path& {
    static const auto root = ResolveRoot();
    return root;
}

auto Config::ConfD() -> std::filesystem::path {
    return Root() / "conf.d";
}

auto Config::EnsureDirectories() -> std::filesystem::path {
    std::error_code error;
    std::filesystem::create_directory(Root(), error);
    std::filesystem::create_directory(ConfD(), error);
    return Root();
}

auto Config::File(std::string_view filename) -> std::filesystem::path {
    return Root() / filename;
}

auto Config::LoadText(std::string_view filename) -> std::optional<std::string> {
    const auto path = File(filename);
    LOG_INFO("Loading config: {}", path.string());
    return ReadFile(path);
}

auto Config::ReadFile(const std::filesystem::path& path) -> std::optional<std::string> {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

auto Config::ParseFile(const std::filesystem::path& path) -> Result<nlohmann::json> {
    const auto contents = ReadFile(path);
    if (!contents) {
        return Unexpected(std::format("Failed to read {}", path.string()));
    }
    const auto extension = path.extension().string();
    if (extension == ".json") {
        auto value = nlohmann::json::parse(*contents, nullptr, false, true);
        if (value.is_discarded()) {
            return Unexpected(std::format("Invalid JSON in {}", path.string()));
        }
        return value;
    }
    if (extension == ".yaml" || extension == ".yml") {
        return Yaml::Parse(*contents).transform_error([&](const Error& error) -> Error {
            return error.WithContext(path.string());
        });
    }
    return Unexpected(std::format("Unsupported config file format: {}", path.string()));
}

auto Config::Merge(nlohmann::json& target, const nlohmann::json& source) -> void {
    if (!source.is_object()) {
        target = source;
        return;
    }
    if (!target.is_object()) {
        target = nlohmann::json::object();
    }
    for (const auto& [key, value] : source.items()) {
        Merge(target[key], value);
    }
}

auto Config::LoadWithConfD(std::string_view root_filename) -> nlohmann::json {
    const auto conf_d = ConfD();

    std::vector<std::filesystem::path> sources;
    for (const auto& base : {Root(), conf_d}) {
        if (auto path = base / root_filename; std::filesystem::exists(path)) {
            sources.push_back(std::move(path));
        }
    }

    std::error_code error;
    std::vector<std::filesystem::path> conf_d_entries;
    for (const auto& entry : std::filesystem::directory_iterator(conf_d, error)) {
        conf_d_entries.push_back(entry.path());
    }
    std::ranges::sort(conf_d_entries);
    sources.insert(sources.end(), conf_d_entries.begin(), conf_d_entries.end());

    auto merged = nlohmann::json::object();
    for (const auto& path : sources) {
        LOG_INFO("Loading config file: {}", path.string());
        auto value = ParseFile(path);
        if (!value) {
            FatalError(std::format("Failed to build settings: {}", value.error().Message()));
        }
        if (!value->is_object() && !value->is_null()) {
            FatalError(std::format("Failed to build settings: {} does not contain a table", path.string()));
        }
        Merge(merged, *value);
    }
    return merged;
}

auto from_json(const nlohmann::json& json, Handedness& value) -> void {
    const auto name = Json::VariantName(json);
    if (name == "Left") {
        value = Handedness::Left;
    } else if (name == "Right") {
        value = Handedness::Right;
    } else {
        throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}`, expected `Left` or `Right`", name), &json);
    }
}

auto to_json(nlohmann::json& json, const Handedness& value) -> void {
    json = value == Handedness::Left ? "Left" : "Right";
}

auto nlohmann::adl_serializer<std::map<ToastTopic, ToastDisplay>>::from_json(
    const nlohmann::json& json,
    std::map<ToastTopic, ToastDisplay>& value
) -> void {
    value.clear();
    for (const auto& [key, display] : json.items()) {
        value[nlohmann::json(key).get<ToastTopic>()] = display.get<ToastDisplay>();
    }
}

auto nlohmann::adl_serializer<std::map<ToastTopic, ToastDisplay>>::to_json(
    nlohmann::json& json,
    const std::map<ToastTopic, ToastDisplay>& value
) -> void {
    json = nlohmann::json::object();
    for (const auto& [topic, display] : value) {
        json[nlohmann::json(topic).get<std::string>()] = display;
    }
}

auto Config::DefaultTimezones() -> std::vector<std::string> {
    constexpr std::chrono::seconds kEmea{-60 * 60};
    constexpr std::chrono::seconds kApac{5 * 60 * 60};

    const auto offset = std::chrono::current_zone()->get_info(std::chrono::system_clock::now()).offset;
    if (offset < kEmea) {
        return {"Europe/Paris", "Asia/Tokyo"};
    }
    if (offset < kApac) {
        return {"America/New_York", "Asia/Tokyo"};
    }
    return {"Europe/Paris", "America/New_York"};
}

auto Config::Load() -> Config {
    const auto tree = Config::LoadWithConfD("config.yaml");
    Config config;
    try {
        tree.get_to(config);
    } catch (const nlohmann::json::exception& exception) {
        FatalError(std::format("Failed to deserialize settings: {}", exception.what()));
    }
    SanitizeRange("keyboard_scale", config.keyboard_scale, 0.05f, 5.0f);
    SanitizeRange("desktop_view_scale", config.desktop_view_scale, 0.05f, 5.0f);
    return config;
}

auto Config::SaveSettings() const -> Result<> {
    nlohmann::json settings;
    settings["watch_pos"] = watch_pos;
    settings["watch_rot"] = watch_rot;
    settings["watch_hand"] = watch_hand;
    settings["watch_view_angle_min"] = Json::ShortestFloat(watch_view_angle_min);
    settings["watch_view_angle_max"] = Json::ShortestFloat(watch_view_angle_max);
    settings["notifications_enabled"] = notifications_enabled;
    settings["notifications_sound_enabled"] = notifications_sound_enabled;
    settings["allow_sliding"] = allow_sliding;
    settings["keyboard_profile"] = keyboard_profile;
    settings["keyboard_region"] = keyboard_region;
    settings["theme"] = theme;
    settings["watch_profile"] = watch_profile;
    settings["space_drag_multiplier"] = Json::ShortestFloat(space_drag_multiplier);
    return WriteJson(Config::ConfD() / "zz-saved-config.json", settings);
}

auto Config::SaveLayout() const -> Result<> {
    nlohmann::json state;
    state["show_screens"] = show_screens;
    state["curve_values"] = ToFloatPairs(curve_values);
    state["transform_values"] = transform_values;
    return WriteJson(Config::ConfD() / "zz-saved-state.json", state);
}
