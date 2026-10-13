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

#include "FormTheme.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <memory>
#include <system_error>
#include <utility>

#include "Config.hpp"
#include "ConfigStore.hpp"
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

auto ParseHexByte(std::string_view text) -> std::optional<uint8_t> {
    uint8_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

auto RoundRadius(Shape shape) -> float {
    switch (shape) {
    case Shape::Panel: return 16.0f;
    case Shape::Control: return 8.0f;
    case Shape::Key: return 10.0f;
    }
    return 0.0f;
}

} // namespace

FormTheme* g_themes = nullptr;

auto FormTheme::Parse(std::string_view text) -> Result<ThemeDefinition> {
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

auto FormTheme::Lookup(const ThemeDefinition& theme, std::string_view color) -> std::optional<std::string> {
    const auto found = std::ranges::find(theme.colors, color, &ThemeColor::name);
    return found == theme.colors.end() ? std::nullopt : std::optional(found->hex);
}

auto FormTheme::Radius(Rounding rounding, Shape shape) -> float {
    switch (rounding) {
    case Rounding::Square: return 0.0f;
    case Rounding::Subtle: return RoundRadius(shape) * 0.5f;
    case Rounding::Round: return RoundRadius(shape);
    }
    return 0.0f;
}

auto FormTheme::Resolve(std::optional<float> custom, Rounding rounding, Shape shape) -> float {
    return custom.value_or(Radius(rounding, shape));
}

auto FormTheme::Name(Rounding rounding) -> std::string_view {
    for (const auto& [value, name] : kRoundingNames) {
        if (value == rounding) {
            return name;
        }
    }
    return kRoundingNames.back().second;
}

auto FormTheme::Next(Rounding rounding) -> Rounding {
    switch (rounding) {
    case Rounding::Square: return Rounding::Subtle;
    case Rounding::Subtle: return Rounding::Round;
    case Rounding::Round: return Rounding::Square;
    }
    return Rounding::Round;
}

auto FormTheme::Token(std::string_view name) -> std::string {
    return std::format("{}{}", kToken, name);
}

auto FormTheme::Resolve(std::string_view color, const ThemeDefinition& theme) -> std::string {
    if (!color.starts_with(kToken)) {
        return std::string(color);
    }
    color.remove_prefix(1);
    if (auto found = Lookup(theme, color)) {
        return std::move(*found);
    }
    LOG_WARN("Unknown theme color @{}", color);
    return "#ff00ff";
}

auto FormTheme::ParseColor(std::string_view color) -> Result<Eigen::Vector4f> {
    return ParseColor(color, g_themes->Active());
}

auto FormTheme::ParseColor(std::string_view color, const ThemeDefinition& theme) -> Result<Eigen::Vector4f> {
    const auto resolved = Resolve(color, theme);
    const std::string_view html_hex = resolved;
    if (html_hex.size() == 7 || html_hex.size() == 9) {
        const auto r = ParseHexByte(html_hex.substr(1, 2));
        const auto g = ParseHexByte(html_hex.substr(3, 2));
        const auto b = ParseHexByte(html_hex.substr(5, 2));
        const auto a = html_hex.size() == 9 ? ParseHexByte(html_hex.substr(7, 2)) : std::optional<uint8_t>(255);
        if (r && g && b && a) {
            return Eigen::Vector4f(
                static_cast<float>(*r) / 255.0f,
                static_cast<float>(*g) / 255.0f,
                static_cast<float>(*b) / 255.0f,
                html_hex.size() == 9 ? static_cast<float>(*a) / 255.0f : 1.0f
            );
        }
    }
    return Unexpected(std::format("Invalid color string: '{}', must be 7 characters long (e.g. #FF00FF)", html_hex));
}

auto FormTheme::FallbackColor() -> Eigen::Vector4f {
    return {1.0f, 0.0f, 1.0f, 1.0f};
}

auto FormTheme::Rgba(std::string_view color) -> Eigen::Vector4f {
    return ParseColor(color).value_or(FallbackColor());
}

auto FormTheme::Readable(const Eigen::Vector4f& background) -> Eigen::Vector4f {
    const float luminance = 0.2126f * background.x() + 0.7152f * background.y() + 0.0722f * background.z();
    return luminance > 0.5f ? Rgba("#181926") : Rgba("#eff1f5");
}

auto FormTheme::Color(std::string_view color) -> std::string {
    return Resolve(color, Active());
}

auto FormTheme::Load(std::span<const std::string_view> built_in, const std::filesystem::path& user_directory)
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
        auto theme = FormTheme::Parse(text);
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
        auto theme = FormTheme::Parse(*text);
        if (!theme) {
            LOG_ERROR("Skipping theme {}: {}", file.string(), theme.error().Message());
            continue;
        }
        theme->source = file;
        add(std::move(*theme));
    }
    return themes;
}

auto FormTheme::Save(const ThemeDefinition& theme) -> Result<> {
    return ConfigStore::WriteFile(UserDirectory() / std::format("{}.yaml", theme.name), Yaml::Emit(nlohmann::ordered_json(theme)));
}

auto FormTheme::UserDirectory() -> std::filesystem::path {
    return ConfigStore::Root() / "themes";
}

auto FormTheme::All() -> const std::vector<ThemeDefinition>& {
    if (!m_themes) {
        m_themes = Load(kBuiltInThemes, UserDirectory());
    }
    return *m_themes;
}

auto FormTheme::Reload() -> void {
    m_themes.reset();
}

auto FormTheme::Find(std::string_view name) -> const ThemeDefinition& {
    const auto& themes = All();
    const auto found = std::ranges::find(themes, name, &ThemeDefinition::name);
    return found == themes.end() ? themes.front() : *found;
}

auto FormTheme::IsBuiltIn(std::string_view name) -> bool {
    return std::ranges::any_of(kBuiltInThemes, [name](std::string_view text) -> bool {
        const auto theme = FormTheme::Parse(text);
        return theme && theme->name == name;
    });
}

auto FormTheme::Active() -> const ThemeDefinition& {
    if (!m_active) {
        m_active = All().front();
    }
    return *m_active;
}

auto FormTheme::SetActive(ThemeDefinition theme) -> void {
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
    json = FormTheme::Name(value);
}

auto from_json(const nlohmann::json& json, ThemeDefinition& value) -> void {
    value.name = json.at("name").get<std::string>();
    if (const auto it = json.find("rounding"); it != json.end() && !it->is_null()) {
        value.rounding = it->get<Rounding>();
    }
    value.font.reset();
    if (const auto it = json.find("font"); it != json.end() && it->is_string()) {
        value.font = it->get<std::string>();
    }
    value.colors.clear();
    for (const auto& [name, hex] : json.at("colors").items()) {
        value.colors.push_back({.name = name, .hex = hex.get<std::string>()});
    }
}

auto to_json(nlohmann::ordered_json& json, const ThemeDefinition& value) -> void {
    json = nlohmann::ordered_json::object();
    json["name"] = value.name;
    json["rounding"] = FormTheme::Name(value.rounding);
    if (value.font) {
        json["font"] = *value.font;
    }
    auto colors = nlohmann::ordered_json::object();
    for (const auto& color : value.colors) {
        colors[color.name] = color.hex;
    }
    json["colors"] = std::move(colors);
}
