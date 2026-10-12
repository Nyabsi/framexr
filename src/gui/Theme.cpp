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

#include "Theme.hpp"

#include <algorithm>
#include <format>
#include <memory>
#include <system_error>
#include <utility>

#include "Config.hpp"
#include "Json.hpp"
#include "Logger.hpp"
#include "Yaml.hpp"

namespace {

constexpr std::array<std::pair<Rounding, std::string_view>, 3> kRoundingNames{{
    {Rounding::Square, "Square"},
    {Rounding::Subtle, "Subtle"},
    {Rounding::Round, "Round"},
}};

constexpr char kMacchiatoYaml[] = {
#embed "themes/macchiato.yaml"
    ,
    '\0'
};

constexpr char kLatteYaml[] = {
#embed "themes/latte.yaml"
    ,
    '\0'
};

constexpr auto kBuiltInThemes = std::to_array<std::string_view>({kMacchiatoYaml, kLatteYaml});

auto RoundRadius(Shape shape) -> float {
    switch (shape) {
    case Shape::Panel: return 16.0f;
    case Shape::Control: return 8.0f;
    case Shape::Key: return 10.0f;
    }
    return 0.0f;
}

} // namespace

Theme* g_themes = nullptr;

auto Theme::Parse(std::string_view text) -> Result<ThemeDefinition> {
    ASSIGN_OR_RETURN(const auto parsed, Yaml::Parse(text));
    try {
        auto theme = parsed.get<ThemeDefinition>();
        if (theme.colors.empty()) {
            return Unexpected(std::format("Theme {} has no colors", theme.name));
        }
        return theme;
    } catch (const nlohmann::json::exception& error) {
        return Unexpected(error.what());
    }
}

auto Theme::Lookup(const ThemeDefinition& theme, std::string_view color) -> std::optional<std::string> {
    const auto found = std::ranges::find(theme.colors, color, &ThemeColor::name);
    return found == theme.colors.end() ? std::nullopt : std::optional(found->hex);
}

auto Theme::Radius(Rounding rounding, Shape shape) -> float {
    switch (rounding) {
    case Rounding::Square: return 0.0f;
    case Rounding::Subtle: return RoundRadius(shape) * 0.5f;
    case Rounding::Round: return RoundRadius(shape);
    }
    return 0.0f;
}

auto Theme::Resolve(std::optional<float> custom, Rounding rounding, Shape shape) -> float {
    return custom.value_or(Radius(rounding, shape));
}

auto Theme::Name(Rounding rounding) -> std::string_view {
    for (const auto& [value, name] : kRoundingNames) {
        if (value == rounding) {
            return name;
        }
    }
    return kRoundingNames.back().second;
}

auto Theme::Next(Rounding rounding) -> Rounding {
    switch (rounding) {
    case Rounding::Square: return Rounding::Subtle;
    case Rounding::Subtle: return Rounding::Round;
    case Rounding::Round: return Rounding::Square;
    }
    return Rounding::Round;
}

auto Theme::Token(std::string_view name) -> std::string {
    return std::format("{}{}", kToken, name);
}

auto Theme::Color(std::string_view color) -> std::string {
    if (!color.starts_with(kToken)) {
        return std::string(color);
    }
    color.remove_prefix(1);
    if (auto found = Lookup(Active(), color)) {
        return std::move(*found);
    }
    LOG_WARN("Unknown theme color @{}", color);
    return "#ff00ff";
}

auto Theme::Load(std::span<const std::string_view> built_in, const std::filesystem::path& user_directory)
    -> std::vector<ThemeDefinition> {
    std::vector<ThemeDefinition> themes;
    const auto add = [&themes](ThemeDefinition theme) -> void {
        const auto existing = std::ranges::find(themes, theme.name, &ThemeDefinition::name);
        if (existing != themes.end()) {
            *existing = std::move(theme);
        } else {
            themes.push_back(std::move(theme));
        }
    };
    for (const auto text : built_in) {
        auto theme = Theme::Parse(text);
        if (!theme) {
            FatalError(std::format("Invalid built-in theme: {}", theme.error().Message()));
        }
        add(std::move(*theme));
    }
    std::error_code error;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(user_directory, error)) {
        if (entry.path().extension() == ".yaml") {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);
    for (const auto& file : files) {
        const auto text = ConfigStore::ReadFile(file);
        if (!text) {
            continue;
        }
        auto theme = Theme::Parse(*text);
        if (!theme) {
            LOG_ERROR("Skipping theme {}: {}", file.string(), theme.error().Message());
            continue;
        }
        theme->source = file;
        add(std::move(*theme));
    }
    return themes;
}

auto Theme::UserDirectory() -> std::filesystem::path {
    return ConfigStore::Root() / "themes";
}

auto Theme::All() -> const std::vector<ThemeDefinition>& {
    if (!m_themes) {
        m_themes = Load(kBuiltInThemes, UserDirectory());
    }
    return *m_themes;
}

auto Theme::Reload() -> void {
    m_themes.reset();
}

auto Theme::Find(std::string_view name) -> const ThemeDefinition& {
    const auto& themes = All();
    const auto found = std::ranges::find(themes, name, &ThemeDefinition::name);
    return found == themes.end() ? themes.front() : *found;
}

auto Theme::IsBuiltIn(std::string_view name) -> bool {
    return std::ranges::any_of(kBuiltInThemes, [name](std::string_view text) -> bool {
        const auto theme = Theme::Parse(text);
        return theme && theme->name == name;
    });
}

auto Theme::Active() -> const ThemeDefinition& {
    if (!m_active) {
        m_active = All().front();
    }
    return *m_active;
}

auto Theme::SetActive(ThemeDefinition theme) -> void {
    m_active = std::move(theme);
}

auto from_json(const nlohmann::json& json, Rounding& value) -> void {
    const auto name = Json::VariantName(json);
    for (const auto& [rounding, rounding_name] : kRoundingNames) {
        if (rounding_name == name) {
            value = rounding;
            return;
        }
    }
    throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for Rounding", name), &json);
}

auto to_json(nlohmann::json& json, const Rounding& value) -> void {
    json = Theme::Name(value);
}

auto from_json(const nlohmann::json& json, ThemeDefinition& value) -> void {
    value.name = json.at("name").get<std::string>();
    if (const auto it = json.find("rounding"); it != json.end() && !it->is_null()) {
        value.rounding = it->get<Rounding>();
    }
    value.colors.clear();
    for (const auto& [name, hex] : json.at("colors").items()) {
        value.colors.push_back({.name = name, .hex = hex.get<std::string>()});
    }
}

auto to_json(nlohmann::ordered_json& json, const ThemeDefinition& value) -> void {
    json = nlohmann::ordered_json::object();
    json["name"] = value.name;
    json["rounding"] = Theme::Name(value.rounding);
    auto colors = nlohmann::ordered_json::object();
    for (const auto& color : value.colors) {
        colors[color.name] = color.hex;
    }
    json["colors"] = std::move(colors);
}
