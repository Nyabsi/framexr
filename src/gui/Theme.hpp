// FrameXR
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
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

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "Error.hpp"

enum class Rounding : uint8_t {
    Square,
    Subtle,
    Round,
};

enum class Shape : uint8_t {
    Panel,
    Control,
    Key,
};

struct ThemeColor {
    std::string name;
    std::string hex;
};

struct ThemeDefinition {
    std::string name;
    Rounding rounding = Rounding::Round;
    std::vector<ThemeColor> colors;
    std::filesystem::path source;

    static auto Parse(std::string_view text) -> Result<ThemeDefinition>;

    [[nodiscard]] auto Lookup(std::string_view color) const -> std::optional<std::string>;
};

class Theme final {
  public:
    static constexpr char kToken = '@';

    static auto Radius(Rounding rounding, Shape shape) -> float;
    static auto Resolve(std::optional<float> custom, Rounding rounding, Shape shape) -> float;
    static auto Name(Rounding rounding) -> std::string_view;
    static auto Next(Rounding rounding) -> Rounding;
    static auto Color(std::string_view color) -> std::string;
    static auto Token(std::string_view name) -> std::string;

    static auto Active() -> const ThemeDefinition&;
    static auto SetActive(ThemeDefinition theme) -> void;
    static auto All() -> const std::vector<ThemeDefinition>&;
    static auto Find(std::string_view name) -> const ThemeDefinition&;
    static auto Reload() -> void;
    static auto UserDirectory() -> std::filesystem::path;
    static auto IsBuiltIn(std::string_view name) -> bool;
    static auto Load(std::span<const std::string_view> built_in, const std::filesystem::path& user_directory)
        -> std::vector<ThemeDefinition>;
};

auto from_json(const nlohmann::json& json, Rounding& value) -> void;
auto to_json(nlohmann::json& json, const Rounding& value) -> void;
auto from_json(const nlohmann::json& json, ThemeDefinition& value) -> void;
auto to_json(nlohmann::ordered_json& json, const ThemeDefinition& value) -> void;
