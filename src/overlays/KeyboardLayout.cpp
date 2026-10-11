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

#include "KeyboardLayout.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <iterator>
#include <system_error>
#include <utility>

#include "Config.hpp"
#include "Json.hpp"
#include "Logger.hpp"
#include "Yaml.hpp"

namespace {

constexpr float kEpsilon = 0.001f;

constexpr char kKeychronV1Yaml[] = {
#embed "keyboards/keychron-v1-ansi.yaml"
    ,
    '\0'
};

constexpr char kFullSizeYaml[] = {
#embed "keyboards/full-size.yaml"
    ,
    '\0'
};

constexpr auto kBuiltInProfiles = std::to_array<std::string_view>({kKeychronV1Yaml, kFullSizeYaml});

constexpr auto kAltModifierNames = std::to_array<std::pair<AltModifier, std::string_view>>({
    {AltModifier::None, "None"},
    {AltModifier::Shift, "Shift"},
    {AltModifier::Ctrl, "Ctrl"},
    {AltModifier::Alt, "Alt"},
    {AltModifier::Super, "Super"},
    {AltModifier::Meta, "Meta"},
});

struct UsLabel {
    VirtualKey key;
    std::string_view lower;
    std::string_view upper;
};

constexpr auto kUsLabels = std::to_array<UsLabel>({
    {VirtualKey::N1, "1", "!"},     {VirtualKey::N2, "2", "@"},       {VirtualKey::N3, "3", "#"},     {VirtualKey::N4, "4", "$"},
    {VirtualKey::N5, "5", "%"},     {VirtualKey::N6, "6", "^"},       {VirtualKey::N7, "7", "&"},     {VirtualKey::N8, "8", "*"},
    {VirtualKey::N9, "9", "("},     {VirtualKey::N0, "0", ")"},       {VirtualKey::Minus, "-", "_"},  {VirtualKey::Plus, "=", "+"},
    {VirtualKey::Comma, " ,", "<"}, {VirtualKey::Period, " .", ">"},  {VirtualKey::Oem1, " ;", ":"},  {VirtualKey::Oem2, " /", "?"},
    {VirtualKey::Oem3, "`", "~"},   {VirtualKey::Oem4, " [", "{"},    {VirtualKey::Oem5, " \\", "|"}, {VirtualKey::Oem6, " ]", "}"},
    {VirtualKey::Oem7, " '", "\""}, {VirtualKey::Oem102, " \\", "|"},
});

auto ToLower(std::string_view text) -> std::string {
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
    return result;
}

auto IsStep(float value) -> bool {
    const float steps = value / kKeyStep;
    return std::abs(steps - std::round(steps)) < kEpsilon;
}

auto OptionalActions(const nlohmann::json& json, std::string_view key) -> std::vector<ButtonAction> {
    const auto it = json.find(key);
    return it == json.end() || it->is_null() ? std::vector<ButtonAction>{} : it->get<std::vector<ButtonAction>>();
}

auto ParseKey(const nlohmann::json& json) -> KeyDefinition {
    const auto at = json.at("at").get<std::array<float, 2>>();
    KeyDefinition key{.x = at[0], .row = static_cast<int>(at[1])};
    if (const auto width = json.find("width"); width != json.end()) {
        key.width = width->get<float>();
    }
    if (const auto name = json.find("key"); name != json.end()) {
        const auto key_name = name->get<std::string>();
        const auto virtual_key = VirtualKeys::Parse(key_name);
        if (!virtual_key) {
            throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for VirtualKey", key_name), &json);
        }
        key.action = TypeKey{.key = *virtual_key};
    } else if (const auto action = json.find("action"); action != json.end()) {
        const auto action_name = action->get<std::string>();
        if (action_name == "Profile") {
            key.action = ProfileKey{};
        } else if (action_name == "Blank") {
            key.action = BlankKey{};
        } else {
            throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for key action", action_name), &json);
        }
    } else if (json.contains("press") || json.contains("release")) {
        key.action = ActionKey{.press = OptionalActions(json, "press"), .release = OptionalActions(json, "release")};
    }
    if (const auto label = json.find("label"); label != json.end() && !label->is_null()) {
        key.label = label->get<std::vector<std::string>>();
    }
    if (const auto shifted = json.find("shifted_label"); shifted != json.end() && !shifted->is_null()) {
        key.shifted_label = shifted->get<std::string>();
    }
    if (const auto style = json.find("style"); style != json.end() && !style->is_null()) {
        if (const auto bg = style->find("bg_color"); bg != style->end()) {
            key.style.bg_color = bg->get<std::string>();
        }
        if (const auto fg = style->find("fg_color"); fg != style->end()) {
            key.style.fg_color = fg->get<std::string>();
        }
        if (const auto radius = style->find("corner_radius"); radius != style->end()) {
            key.style.corner_radius = radius->get<float>();
        }
    }
    return key;
}

auto KeyJson(const KeyDefinition& key) -> nlohmann::ordered_json {
    nlohmann::ordered_json json;
    json["at"] = {Json::ShortestFloat(key.x), key.row};
    if (std::abs(key.width - 1.0f) > kEpsilon) {
        json["width"] = Json::ShortestFloat(key.width);
    }
    std::visit(
        [&json](const auto& action) -> void {
            using Action = std::decay_t<decltype(action)>;
            if constexpr (std::is_same_v<Action, TypeKey>) {
                json["key"] = VirtualKeys::Name(action.key);
            } else if constexpr (std::is_same_v<Action, ProfileKey>) {
                json["action"] = "Profile";
            } else if constexpr (std::is_same_v<Action, BlankKey>) {
                json["action"] = "Blank";
            } else {
                if (!action.press.empty()) {
                    json["press"] = action.press;
                }
                if (!action.release.empty()) {
                    json["release"] = action.release;
                }
            }
        },
        key.action
    );
    if (key.label) {
        json["label"] = *key.label;
    }
    if (key.shifted_label) {
        json["shifted_label"] = *key.shifted_label;
    }
    if (!key.style.Empty()) {
        nlohmann::ordered_json style;
        if (key.style.bg_color) {
            style["bg_color"] = *key.style.bg_color;
        }
        if (key.style.fg_color) {
            style["fg_color"] = *key.style.fg_color;
        }
        if (key.style.corner_radius) {
            style["corner_radius"] = Json::ShortestFloat(*key.style.corner_radius);
        }
        json["style"] = std::move(style);
    }
    return json;
}

auto ProfileStore() -> std::optional<std::vector<KeyboardLayout>>& {
    static std::optional<std::vector<KeyboardLayout>> profiles;
    return profiles;
}

} // namespace

auto from_json(const nlohmann::json& json, AltModifier& value) -> void {
    const auto name = Json::VariantName(json);
    for (const auto& [modifier, modifier_name] : kAltModifierNames) {
        if (modifier_name == name) {
            value = modifier;
            return;
        }
    }
    throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for AltModifier", name), &json);
}

auto from_json(const nlohmann::json& json, KeyboardLayout& value) -> void {
    value = KeyboardLayout{
        .name = json.at("name").get<std::string>(),
        .columns = json.at("columns").get<float>(),
        .rows = json.at("rows").get<int>(),
    };
    if (const auto alt = json.find("alt_modifier"); alt != json.end() && !alt->is_null()) {
        value.alt_modifier = alt->get<AltModifier>();
    }
    if (const auto it = json.find("auto_labels"); it != json.end() && !it->is_null()) {
        value.auto_labels = it->get<bool>();
    }
    if (const auto it = json.find("region"); it != json.end() && !it->is_null()) {
        value.region = it->get<std::string>();
    }
    for (const auto& key : json.at("keys")) {
        value.keys.push_back(ParseKey(key));
    }
    value.SortKeys();
}

auto to_json(nlohmann::ordered_json& json, const KeyboardLayout& value) -> void {
    json = nlohmann::ordered_json::object();
    json["name"] = value.name;
    json["columns"] = Json::ShortestFloat(value.columns);
    json["rows"] = value.rows;
    for (const auto& [modifier, name] : kAltModifierNames) {
        if (modifier == value.alt_modifier) {
            json["alt_modifier"] = name;
        }
    }
    if (value.auto_labels) {
        json["auto_labels"] = *value.auto_labels;
    }
    if (value.region) {
        json["region"] = *value.region;
    }
    auto keys = nlohmann::ordered_json::array();
    for (const auto& key : value.keys) {
        keys.push_back(KeyJson(key));
    }
    json["keys"] = std::move(keys);
}

auto KeyboardLayout::Parse(std::string_view text) -> Result<KeyboardLayout> {
    ASSIGN_OR_RETURN(const auto parsed, Yaml::Parse(text));
    KeyboardLayout layout;
    try {
        layout = parsed.get<KeyboardLayout>();
    } catch (const nlohmann::json::exception& error) {
        return Unexpected(error.what());
    }
    RETURN_IF_ERROR(layout.Validate());
    return layout;
}

auto KeyboardLayout::DefaultLabel(std::string_view key) -> std::vector<std::string> {
    if (key.empty()) {
        return {};
    }
    if (key.size() == 1) {
        return {ToLower(key)};
    }
    if (key.starts_with("KP_")) {
        key.remove_prefix(3);
    }
    if (const auto underscore = key.find('_'); underscore != std::string_view::npos) {
        key = key.substr(0, underscore);
    }
    if (key.empty()) {
        return {};
    }
    return {std::format("{}{}", static_cast<char>(std::toupper(static_cast<unsigned char>(key.front()))), ToLower(key.substr(1)))};
}

auto KeyboardLayout::Snap(float units) -> float {
    return std::round(units / kKeyStep) * kKeyStep;
}

auto KeyboardLayout::Validate() const -> Result<> {
    if (columns <= 0.0f || rows <= 0) {
        return Unexpected(std::format("Keyboard {} needs at least one row and column", name));
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        const auto& key = keys[i];
        if (key.width < kKeyStep - kEpsilon || !IsStep(key.width) || !IsStep(key.x)) {
            return Unexpected(std::format("Key {} in row {} must sit on and span quarter units", i, key.row));
        }
        if (key.row < 0 || key.row >= rows || key.x < -kEpsilon || key.Right() > columns + kEpsilon) {
            return Unexpected(std::format("Key at {} in row {} is outside the {}x{} grid", key.x, key.row, columns, rows));
        }
        for (size_t j = i + 1; j < keys.size(); ++j) {
            const auto& other = keys[j];
            if (other.row == key.row && other.x < key.Right() - kEpsilon && key.x < other.Right() - kEpsilon) {
                return Unexpected(std::format("Keys at {} and {} overlap in row {}", key.x, other.x, key.row));
            }
        }
    }
    return {};
}

auto KeyboardLayout::KeyAt(float x, int row) const -> std::optional<size_t> {
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].row == row && x >= keys[i].x - kEpsilon && x < keys[i].Right() - kEpsilon) {
            return i;
        }
    }
    return std::nullopt;
}

auto KeyboardLayout::Fits(float x, int row, float width, std::optional<size_t> ignore) const -> bool {
    if (row < 0 || row >= rows || x < -kEpsilon || x + width > columns + kEpsilon || width < kKeyStep - kEpsilon) {
        return false;
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        const auto& key = keys[i];
        if (i != ignore && key.row == row && key.x < x + width - kEpsilon && x < key.Right() - kEpsilon) {
            return false;
        }
    }
    return true;
}

auto KeyboardLayout::FreeCells() const -> std::vector<std::pair<float, int>> {
    std::vector<std::pair<float, int>> cells;
    const auto whole_columns = static_cast<int>(std::floor(columns + kEpsilon));
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < whole_columns; ++column) {
            if (const auto x = static_cast<float>(column); Fits(x, row, 1.0f)) {
                cells.emplace_back(x, row);
            }
        }
    }
    return cells;
}

auto KeyboardLayout::Labels(const KeyDefinition& key) const -> std::vector<std::string> {
    if (key.label) {
        return *key.label;
    }
    return std::visit(
        [](const auto& action) -> std::vector<std::string> {
            using Action = std::decay_t<decltype(action)>;
            if constexpr (std::is_same_v<Action, TypeKey>) {
                if (const auto* us = std::ranges::find(kUsLabels, action.key, &UsLabel::key); us != kUsLabels.end()) {
                    return {std::string(us->lower), std::string(us->upper)};
                }
                return DefaultLabel(VirtualKeys::Name(action.key));
            } else if constexpr (std::is_same_v<Action, ProfileKey>) {
                return {"Layout"};
            } else {
                return {};
            }
        },
        key.action
    );
}

auto KeyboardLayout::AltModifierMask() const -> uint8_t {
    switch (alt_modifier) {
    case AltModifier::Shift: return kModShift;
    case AltModifier::Ctrl: return kModCtrl;
    case AltModifier::Alt: return kModAlt;
    case AltModifier::Super: return kModSuper;
    case AltModifier::Meta: return kModMeta;
    case AltModifier::None: break;
    }
    return 0;
}

auto KeyboardLayout::SortKeys() -> void {
    std::ranges::stable_sort(keys, [](const KeyDefinition& a, const KeyDefinition& b) -> bool {
        return a.row != b.row ? a.row < b.row : a.x < b.x;
    });
}

auto KeyboardProfiles::Load(std::span<const std::string_view> built_in, const std::filesystem::path& user_directory)
    -> std::vector<KeyboardLayout> {
    std::vector<KeyboardLayout> profiles;
    const auto add = [&profiles](KeyboardLayout layout) -> void {
        const auto existing = std::ranges::find(profiles, layout.name, &KeyboardLayout::name);
        if (existing != profiles.end()) {
            *existing = std::move(layout);
        } else {
            profiles.push_back(std::move(layout));
        }
    };

    for (const auto text : built_in) {
        auto layout = KeyboardLayout::Parse(text);
        if (!layout) {
            FatalError(std::format("Invalid built-in keyboard profile: {}", layout.error().Message()));
        }
        add(std::move(*layout));
    }

    std::error_code error;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(user_directory, error)) {
        const auto extension = entry.path().extension();
        if (entry.is_regular_file(error) && (extension == ".yaml" || extension == ".yml")) {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);
    for (const auto& file : files) {
        const auto text = Config::ReadFile(file);
        if (!text) {
            LOG_ERROR("Could not read keyboard profile {}", file.string());
            continue;
        }
        auto layout = KeyboardLayout::Parse(*text);
        if (!layout) {
            LOG_ERROR("Skipping keyboard profile {}: {}", file.string(), layout.error().Message());
            continue;
        }
        LOG_INFO("Loaded keyboard profile {} from {}", layout->name, file.string());
        layout->source = file;
        add(std::move(*layout));
    }
    return profiles;
}

auto KeyboardProfiles::UserDirectory() -> std::filesystem::path {
    return Config::Root() / "keyboards";
}

auto KeyboardProfiles::All() -> const std::vector<KeyboardLayout>& {
    auto& profiles = ProfileStore();
    if (!profiles) {
        profiles = Load(kBuiltInProfiles, UserDirectory());
    }
    return *profiles;
}

auto KeyboardProfiles::Reload() -> void {
    ProfileStore().reset();
}

auto KeyboardProfiles::IsBuiltIn(std::string_view name) -> bool {
    return std::ranges::any_of(kBuiltInProfiles, [name](std::string_view text) -> bool {
        const auto layout = KeyboardLayout::Parse(text);
        return layout && layout->name == name;
    });
}

auto KeyboardProfiles::Find(std::string_view name) -> const KeyboardLayout& {
    const auto& profiles = All();
    const auto found = std::ranges::find(profiles, name, &KeyboardLayout::name);
    if (found == profiles.end()) {
        LOG_WARN("Unknown keyboard profile {}, using {}", name, profiles.front().name);
        return profiles.front();
    }
    return *found;
}

auto KeyboardProfiles::After(std::string_view name) -> const KeyboardLayout& {
    const auto& profiles = All();
    const auto found = std::ranges::find(profiles, name, &KeyboardLayout::name);
    if (found == profiles.end() || std::next(found) == profiles.end()) {
        return profiles.front();
    }
    return *std::next(found);
}
