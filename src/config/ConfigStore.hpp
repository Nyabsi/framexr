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

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "Config.hpp"
#include "Error.hpp"
#include "Logger.hpp"
#include "Yaml.hpp"

class ConfigStore final {
  public:
    static auto Load() -> Config;

    static auto Root() -> const std::filesystem::path&;
    static auto ConfD() -> std::filesystem::path;
    static auto EnsureDirectories() -> std::filesystem::path;
    static auto File(std::string_view filename) -> std::filesystem::path;

    static auto LoadText(std::string_view filename) -> std::optional<std::string>;
    static auto ReadFile(const std::filesystem::path& path) -> std::optional<std::string>;
    static auto WriteFile(const std::filesystem::path& path, std::string_view text) -> Result<>;
    static auto ParseFile(const std::filesystem::path& path) -> Result<nlohmann::json>;
    static auto LoadWithConfD(std::string_view root_filename) -> nlohmann::json;
    static auto Merge(nlohmann::json& target, const nlohmann::json& source) -> void;

    template <typename T>
    static auto LoadKnownYaml(std::string_view filename, std::string_view fallback) -> T;

    static auto PipewireTokens() -> Result<std::map<std::string, std::string>>;
    static auto SavePipewireTokens(const std::map<std::string, std::string>& tokens) -> Result<>;
    static auto SaveSettings() -> Result<>;
    static auto SaveLayout() -> Result<>;
};

template <typename T>
auto ConfigStore::LoadKnownYaml(std::string_view filename, std::string_view fallback) -> T {
    const auto user_text = LoadText(filename);
    std::vector<std::string_view> candidates;
    if (user_text) {
        candidates.emplace_back(*user_text);
    }
    candidates.push_back(fallback);

    for (const auto text : candidates) {
        auto parsed = Yaml::Parse(text);
        if (parsed) {
            try {
                return parsed->template get<T>();
            } catch (const nlohmann::json::exception& error) {
                parsed = Unexpected(error.what());
            }
        }
        LOG_ERROR("Failed to parse {}, falling back to defaults.", filename);
        LOG_ERROR("{}", parsed.error().Message());
    }
    FatalError("No usable config found.");
}
