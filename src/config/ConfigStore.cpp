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

#include "ConfigStore.hpp"

#include <cmath>
#include <format>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#include "Platform.hpp"

namespace
{

auto ResolveRoot() -> std::filesystem::path
{
    if (const auto home = Platform::ConfigHome())
    {
        return *home / "framexr";
    }
    const auto fallback = Platform::RuntimeDirectory() / "framexr";
    LOG_ERROR("Err: Failed to find config path, using {}", fallback.string());
    return fallback;
}

auto SanitizeRange(std::string_view name, float value, float from, float to) -> void
{
    if (!std::isnormal(value) || value < from || value > to)
    {
        FatalError(std::format("Config: {} needs to be between {} and {}", name, from, to));
    }
}

auto WriteJson(const std::filesystem::path& path, const nlohmann::json& value) -> Result<>
{
    std::ofstream stream(path, std::ios::trunc);
    if (!stream)
    {
        return Unexpected(std::format("Failed to write {}", path.string()));
    }
    stream << value.dump(2);
    return {};
}

auto ToFloatPairs(const std::vector<std::pair<std::string, float>>& values) -> nlohmann::json
{
    auto array = nlohmann::json::array();
    for (const auto& [name, value] : values)
    {
        array.push_back({name, Json::ShortestFloat(value)});
    }
    return array;
}

} // namespace

auto ConfigStore::Root() -> const std::filesystem::path&
{
    static const auto root = ResolveRoot();
    return root;
}

auto ConfigStore::ConfD() -> std::filesystem::path
{
    return Root() / "conf.d";
}

auto ConfigStore::EnsureDirectories() -> std::filesystem::path
{
    std::error_code error;
    std::filesystem::create_directory(Root(), error);
    std::filesystem::create_directory(ConfD(), error);
    return Root();
}

auto ConfigStore::File(std::string_view filename) -> std::filesystem::path
{
    return Root() / filename;
}

auto ConfigStore::LoadText(std::string_view filename) -> std::optional<std::string>
{
    const auto path = File(filename);
    LOG_INFO("Loading config: {}", path.string());
    return ReadFile(path);
}

auto ConfigStore::ReadFile(const std::filesystem::path& path) -> std::optional<std::string>
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

auto ConfigStore::WriteFile(const std::filesystem::path& path, std::string_view text) -> Result<>
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream stream(path, std::ios::trunc);
    if (!stream)
    {
        return Unexpected(std::format("Could not write {}", path.string()));
    }
    stream << text;
    return {};
}

auto ConfigStore::ParseFile(const std::filesystem::path& path) -> Result<nlohmann::json>
{
    const auto contents = ReadFile(path);
    if (!contents)
    {
        return Unexpected(std::format("Failed to read {}", path.string()));
    }
    const auto extension = path.extension().string();
    if (extension == ".json")
    {
        auto value = nlohmann::json::parse(*contents, nullptr, false, true);
        if (value.is_discarded())
        {
            return Unexpected(std::format("Invalid JSON in {}", path.string()));
        }
        return value;
    }
    if (extension == ".yaml" || extension == ".yml")
    {
        return Yaml::Parse(*contents).transform_error([&](const Error& error) -> Error { return error.WithContext(path.string()); });
    }
    return Unexpected(std::format("Unsupported config file format: {}", path.string()));
}

auto ConfigStore::Merge(nlohmann::json& target, const nlohmann::json& source) -> void
{
    if (!source.is_object())
    {
        target = source;
        return;
    }
    if (!target.is_object())
    {
        target = nlohmann::json::object();
    }
    for (const auto& [key, value] : source.items())
    {
        Merge(target[key], value);
    }
}

auto ConfigStore::LoadWithConfD(std::string_view root_filename) -> nlohmann::json
{
    const auto conf_d = ConfD();

    std::vector<std::filesystem::path> sources;
    for (const auto& base : {Root(), conf_d})
    {
        if (auto path = base / root_filename; std::filesystem::exists(path))
        {
            sources.push_back(std::move(path));
        }
    }

    std::error_code error;
    std::vector<std::filesystem::path> conf_d_entries;
    for (const auto& entry : std::filesystem::directory_iterator(conf_d, error))
    {
        conf_d_entries.push_back(entry.path());
    }
    std::ranges::sort(conf_d_entries);
    sources.insert(sources.end(), conf_d_entries.begin(), conf_d_entries.end());

    auto merged = nlohmann::json::object();
    for (const auto& path : sources)
    {
        LOG_INFO("Loading config file: {}", path.string());
        auto value = ParseFile(path);
        if (!value)
        {
            FatalError(std::format("Failed to build settings: {}", value.error().Message()));
        }
        if (!value->is_object() && !value->is_null())
        {
            FatalError(std::format("Failed to build settings: {} does not contain a table", path.string()));
        }
        Merge(merged, *value);
    }
    return merged;
}

auto ConfigStore::Load() -> Config
{
    const auto tree = ConfigStore::LoadWithConfD("config.yaml");
    Config config;
    try
    {
        tree.get_to(config);
    }
    catch (const nlohmann::json::exception& exception)
    {
        FatalError(std::format("Failed to deserialize settings: {}", exception.what()));
    }
    SanitizeRange("keyboard_scale", config.keyboard_scale, 0.05f, 5.0f);
    SanitizeRange("desktop_view_scale", config.desktop_view_scale, 0.05f, 5.0f);
    return config;
}

auto ConfigStore::SaveSettings() -> Result<>
{
    const auto& config = *g_config;
    nlohmann::json settings;
    settings["watch_pos"] = config.watch_pos;
    settings["watch_rot"] = config.watch_rot;
    settings["watch_hand"] = config.watch_hand;
    settings["watch_view_angle_min"] = Json::ShortestFloat(config.watch_view_angle_min);
    settings["watch_view_angle_max"] = Json::ShortestFloat(config.watch_view_angle_max);
    settings["notifications_enabled"] = config.notifications_enabled;
    settings["notifications_sound_enabled"] = config.notifications_sound_enabled;
    settings["allow_sliding"] = config.allow_sliding;
    settings["keyboard_profile"] = config.keyboard_profile;
    settings["keyboard_region"] = config.keyboard_region;
    settings["theme"] = config.theme;
    settings["watch_profile"] = config.watch_profile;
    settings["space_drag_multiplier"] = Json::ShortestFloat(config.space_drag_multiplier);
    return WriteJson(ConfigStore::ConfD() / "zz-saved-config.json", settings);
}

auto ConfigStore::SaveLayout() -> Result<>
{
    const auto& config = *g_config;
    nlohmann::json state;
    state["show_screens"] = config.show_screens;
    state["curve_values"] = ToFloatPairs(config.curve_values);
    state["transform_values"] = config.transform_values;
    return WriteJson(ConfigStore::ConfD() / "zz-saved-state.json", state);
}

auto ConfigStore::PipewireTokens() -> Result<std::map<std::string, std::string>>
{
    ASSIGN_OR_RETURN(const auto parsed, ParseFile(ConfD() / "pw_tokens.yaml"));
    std::map<std::string, std::string> tokens;
    if (const auto it = parsed.find("pw_tokens"); it != parsed.end() && it->is_object())
    {
        for (const auto& [display, token] : it->items())
        {
            if (token.is_string())
            {
                tokens.emplace(display, token.get<std::string>());
            }
        }
    }
    return tokens;
}

auto ConfigStore::SavePipewireTokens(const std::map<std::string, std::string>& tokens) -> Result<>
{
    const auto path = ConfD() / "pw_tokens.yaml";
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        return Unexpected(std::format("Failed to open {}", path.string()));
    }
    stream << Yaml::Emit(nlohmann::json{{"pw_tokens", tokens}});
    if (!stream)
    {
        return Unexpected(std::format("Failed to write {}", path.string()));
    }
    return {};
}
