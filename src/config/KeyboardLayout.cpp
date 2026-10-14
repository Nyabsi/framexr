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
#include <iterator>
#include <optional>
#include <system_error>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <utility>

#include "Json.hpp"
#include "Platform.hpp"
#include "Yaml.hpp"
#include "Logger.hpp"
#include "Config.hpp"
#include "ConfigStore.hpp"

namespace {

constexpr unsigned char kKeychronV1Yaml[] = {
#embed "keyboards/keychron-v1-ansi.yaml"
};

constexpr unsigned char kFullSizeYaml[] = {
#embed "keyboards/full-size.yaml"
};

constexpr unsigned char kJisFullSizeYaml[] = {
#embed "keyboards/jis-full-size.yaml"
};

constexpr unsigned char kJisTklYaml[] = {
#embed "keyboards/jis-tkl.yaml"
};

constexpr unsigned char kKoreanFullSizeYaml[] = {
#embed "keyboards/korean-full-size.yaml"
};

constexpr unsigned char kKoreanTklYaml[] = {
#embed "keyboards/korean-tkl.yaml"
};

constexpr unsigned char kChineseFullSizeYaml[] = {
#embed "keyboards/chinese-full-size.yaml"
};

constexpr unsigned char kChineseTklYaml[] = {
#embed "keyboards/chinese-tkl.yaml"
};

auto Text(std::span<const unsigned char> bytes) -> std::string_view {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

auto BuiltInProfiles() -> std::span<const std::string_view> {
    static const auto profiles = std::to_array<std::string_view>({
        Text(kKeychronV1Yaml),
        Text(kFullSizeYaml),
        Text(kJisFullSizeYaml),
        Text(kJisTklYaml),
        Text(kKoreanFullSizeYaml),
        Text(kKoreanTklYaml),
        Text(kChineseFullSizeYaml),
        Text(kChineseTklYaml),
    });
    return profiles;
}

auto Loaded() -> std::optional<std::vector<KeyboardLayout>>& {
    static std::optional<std::vector<KeyboardLayout>> loaded;
    return loaded;
}

constexpr float kEpsilon = 0.001f;

constexpr auto kAltModifierNames = std::to_array<std::pair<AltModifier, std::string_view>>({
    {AltModifier::None, "None"},
    {AltModifier::Shift, "Shift"},
    {AltModifier::Ctrl, "Ctrl"},
    {AltModifier::Alt, "Alt"},
    {AltModifier::Super, "Super"},
    {AltModifier::Meta, "Meta"},
});

constexpr auto kImeScriptNames = std::to_array<std::pair<ImeScript, std::string_view>>({
    {ImeScript::None, "None"},
    {ImeScript::Hangul, "Hangul"},
    {ImeScript::Kana, "Kana"},
    {ImeScript::Cangjie, "Cangjie"},
});

constexpr auto kHangulLabels = std::to_array<FixedLabel>({
    {VirtualKey::Q, "ㅂ", "ㅃ"}, {VirtualKey::W, "ㅈ", "ㅉ"}, {VirtualKey::E, "ㄷ", "ㄸ"}, {VirtualKey::R, "ㄱ", "ㄲ"},
    {VirtualKey::T, "ㅅ", "ㅆ"}, {VirtualKey::Y, "ㅛ", ""},   {VirtualKey::U, "ㅕ", ""},   {VirtualKey::I, "ㅑ", ""},
    {VirtualKey::O, "ㅐ", "ㅒ"}, {VirtualKey::P, "ㅔ", "ㅖ"}, {VirtualKey::A, "ㅁ", ""},   {VirtualKey::S, "ㄴ", ""},
    {VirtualKey::D, "ㅇ", ""},   {VirtualKey::F, "ㄹ", ""},   {VirtualKey::G, "ㅎ", ""},   {VirtualKey::H, "ㅗ", ""},
    {VirtualKey::J, "ㅓ", ""},   {VirtualKey::K, "ㅏ", ""},   {VirtualKey::L, "ㅣ", ""},   {VirtualKey::Z, "ㅋ", ""},
    {VirtualKey::X, "ㅌ", ""},   {VirtualKey::C, "ㅊ", ""},   {VirtualKey::V, "ㅍ", ""},   {VirtualKey::B, "ㅠ", ""},
    {VirtualKey::N, "ㅜ", ""},   {VirtualKey::M, "ㅡ", ""},
});

constexpr auto kKanaLabels = std::to_array<FixedLabel>({
    {VirtualKey::N1, "ぬ", ""},       {VirtualKey::N2, "ふ", ""},     {VirtualKey::N3, "あ", "ぁ"},
    {VirtualKey::N4, "う", "ぅ"},     {VirtualKey::N5, "え", "ぇ"},   {VirtualKey::N6, "お", "ぉ"},
    {VirtualKey::N7, "や", "ゃ"},     {VirtualKey::N8, "ゆ", "ゅ"},   {VirtualKey::N9, "よ", "ょ"},
    {VirtualKey::N0, "わ", "を"},     {VirtualKey::Minus, "ほ", ""},  {VirtualKey::Plus, "へ", ""},
    {VirtualKey::Yen, "ー", ""},      {VirtualKey::Q, "た", ""},      {VirtualKey::W, "て", ""},
    {VirtualKey::E, "い", "ぃ"},      {VirtualKey::R, "す", ""},      {VirtualKey::T, "か", ""},
    {VirtualKey::Y, "ん", ""},        {VirtualKey::U, "な", ""},      {VirtualKey::I, "に", ""},
    {VirtualKey::O, "ら", ""},        {VirtualKey::P, "せ", ""},      {VirtualKey::Oem4, "゛", ""},
    {VirtualKey::Oem6, "゜", "「"},   {VirtualKey::A, "ち", ""},      {VirtualKey::S, "と", ""},
    {VirtualKey::D, "し", ""},        {VirtualKey::F, "は", ""},      {VirtualKey::G, "き", ""},
    {VirtualKey::H, "く", ""},        {VirtualKey::J, "ま", ""},      {VirtualKey::K, "の", ""},
    {VirtualKey::L, "り", ""},        {VirtualKey::Oem1, "れ", ""},   {VirtualKey::Oem7, "け", ""},
    {VirtualKey::Oem5, "む", "」"},   {VirtualKey::Z, "つ", "っ"},    {VirtualKey::X, "さ", ""},
    {VirtualKey::C, "そ", ""},        {VirtualKey::V, "ひ", ""},      {VirtualKey::B, "こ", ""},
    {VirtualKey::N, "み", ""},        {VirtualKey::M, "も", ""},      {VirtualKey::Comma, "ね", "、"},
    {VirtualKey::Period, "る", "。"}, {VirtualKey::Oem2, "め", "・"}, {VirtualKey::AbntC1, "ろ", ""},
});

constexpr auto kCangjieLabels = std::to_array<FixedLabel>({
    {VirtualKey::Q, "手", ""}, {VirtualKey::W, "田", ""}, {VirtualKey::E, "水", ""}, {VirtualKey::R, "口", ""},
    {VirtualKey::T, "廿", ""}, {VirtualKey::Y, "卜", ""}, {VirtualKey::U, "山", ""}, {VirtualKey::I, "戈", ""},
    {VirtualKey::O, "人", ""}, {VirtualKey::P, "心", ""}, {VirtualKey::A, "日", ""}, {VirtualKey::S, "尸", ""},
    {VirtualKey::D, "木", ""}, {VirtualKey::F, "火", ""}, {VirtualKey::G, "土", ""}, {VirtualKey::H, "竹", ""},
    {VirtualKey::J, "十", ""}, {VirtualKey::K, "大", ""}, {VirtualKey::L, "中", ""}, {VirtualKey::Z, "重", ""},
    {VirtualKey::X, "難", ""}, {VirtualKey::C, "金", ""}, {VirtualKey::V, "女", ""}, {VirtualKey::B, "月", ""},
    {VirtualKey::N, "弓", ""}, {VirtualKey::M, "一", ""},
});

auto LocaleTag(std::string_view locale) -> std::string {
    std::string tag(locale.substr(0, locale.find_first_of(".@")));
    std::ranges::replace(tag, '-', '_');
    return tag;
}

auto LocaleMatch(const KeyboardLayout& layout, const std::string& tag) -> int {
    const auto language = std::string_view(tag).substr(0, tag.find('_'));
    int best = 0;
    for (const auto& locale : layout.Locales()) {
        const auto wanted = LocaleTag(locale);
        best = std::max(best, wanted == tag ? 2 : (wanted == language ? 1 : 0));
    }
    return best;
}

auto ImeTable(ImeScript ime) -> std::span<const FixedLabel> {
    switch (ime) {
    case ImeScript::Hangul: return kHangulLabels;
    case ImeScript::Kana: return kKanaLabels;
    case ImeScript::Cangjie: return kCangjieLabels;
    case ImeScript::None: break;
    }
    return {};
}

constexpr auto kUsLabels = std::to_array<FixedLabel>({
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
        } else if (action_name == "Ime") {
            key.action = ImeKey{};
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
            } else if constexpr (std::is_same_v<Action, ImeKey>) {
                json["action"] = "Ime";
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
    if (key.style.bg_color || key.style.fg_color || key.style.corner_radius) {
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

auto from_json(const nlohmann::json& json, ImeScript& value) -> void {
    const auto name = Json::VariantName(json);
    for (const auto& [ime, ime_name] : kImeScriptNames) {
        if (ime_name == name) {
            value = ime;
            return;
        }
    }
    throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for ImeScript", name), &json);
}

auto from_json(const nlohmann::json& json, KeyboardLayout& value) -> void {
    value = KeyboardLayout();
    value.SetName(json.at("name").get<std::string>());
    value.SetColumns(json.at("columns").get<float>());
    value.SetRows(json.at("rows").get<int>());
    if (const auto alt = json.find("alt_modifier"); alt != json.end() && !alt->is_null()) {
        value.SetAltKey(alt->get<AltModifier>());
    }
    if (const auto it = json.find("auto_labels"); it != json.end() && !it->is_null()) {
        value.SetAutoLabels(it->get<bool>());
    }
    if (const auto it = json.find("ime"); it != json.end() && !it->is_null()) {
        value.SetIme(it->get<ImeScript>());
    }
    if (const auto it = json.find("region"); it != json.end() && !it->is_null()) {
        value.SetRegion(it->get<std::string>());
    }
    if (const auto it = json.find("locales"); it != json.end() && !it->is_null()) {
        value.SetLocales(it->get<std::vector<std::string>>());
    }
    for (const auto& key : json.at("keys")) {
        value.Keys().push_back(ParseKey(key));
    }
    value.SortKeys();
}

auto to_json(nlohmann::ordered_json& json, const KeyboardLayout& value) -> void {
    json = nlohmann::ordered_json::object();
    json["name"] = value.Name();
    json["columns"] = Json::ShortestFloat(value.Columns());
    json["rows"] = value.Rows();
    for (const auto& [modifier, name] : kAltModifierNames) {
        if (modifier == value.AltKey()) {
            json["alt_modifier"] = name;
        }
    }
    if (value.AutoLabels()) {
        json["auto_labels"] = *value.AutoLabels();
    }
    for (const auto& [ime, name] : kImeScriptNames) {
        if (ime == value.Ime() && ime != ImeScript::None) {
            json["ime"] = name;
        }
    }
    if (value.Region()) {
        json["region"] = *value.Region();
    }
    if (!value.Locales().empty()) {
        json["locales"] = value.Locales();
    }
    auto keys = nlohmann::ordered_json::array();
    for (const auto& key : value.Keys()) {
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
    if (m_columns <= 0.0f || m_rows <= 0) {
        return Unexpected(std::format("Keyboard {} needs at least one row and column", m_name));
    }
    for (size_t i = 0; i < m_keys.size(); ++i) {
        const auto& key = m_keys[i];
        if (key.width < kKeyStep - kEpsilon || !IsStep(key.width) || !IsStep(key.x)) {
            return Unexpected(std::format("Key {} in row {} must sit on and span quarter units", i, key.row));
        }
        if (key.row < 0 || key.row >= m_rows || key.x < -kEpsilon || KeyboardLayout::RightEdge(key) > m_columns + kEpsilon) {
            return Unexpected(std::format("Key at {} in row {} is outside the {}x{} grid", key.x, key.row, m_columns, m_rows));
        }
        for (size_t j = i + 1; j < m_keys.size(); ++j) {
            const auto& other = m_keys[j];
            if (other.row == key.row && other.x < KeyboardLayout::RightEdge(key) - kEpsilon
                && key.x < KeyboardLayout::RightEdge(other) - kEpsilon) {
                return Unexpected(std::format("Keys at {} and {} overlap in row {}", key.x, other.x, key.row));
            }
        }
    }
    return {};
}

auto KeyboardLayout::KeyAt(float x, int row) const -> std::optional<size_t> {
    for (size_t i = 0; i < m_keys.size(); ++i) {
        if (m_keys[i].row == row && x >= m_keys[i].x - kEpsilon && x < KeyboardLayout::RightEdge(m_keys[i]) - kEpsilon) {
            return i;
        }
    }
    return std::nullopt;
}

auto KeyboardLayout::Fits(float x, int row, float width, std::optional<size_t> ignore) const -> bool {
    if (row < 0 || row >= m_rows || x < -kEpsilon || x + width > m_columns + kEpsilon || width < kKeyStep - kEpsilon) {
        return false;
    }
    for (size_t i = 0; i < m_keys.size(); ++i) {
        const auto& key = m_keys[i];
        if (i != ignore && key.row == row && key.x < x + width - kEpsilon && x < KeyboardLayout::RightEdge(key) - kEpsilon) {
            return false;
        }
    }
    return true;
}

auto KeyboardLayout::FreeCells() const -> std::vector<std::pair<float, int>> {
    std::vector<std::pair<float, int>> cells;
    const auto whole_columns = static_cast<int>(std::floor(m_columns + kEpsilon));
    for (int row = 0; row < m_rows; ++row) {
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
                if (const auto* us = std::ranges::find(kUsLabels, action.key, &FixedLabel::key); us != kUsLabels.end()) {
                    return {std::string(us->lower), std::string(us->upper)};
                }
                return DefaultLabel(VirtualKeys::Name(action.key));
            } else if constexpr (std::is_same_v<Action, ProfileKey>) {
                return {"Layout"};
            } else if constexpr (std::is_same_v<Action, ImeKey>) {
                return {"IME"};
            } else {
                return {};
            }
        },
        key.action
    );
}

auto KeyboardLayout::ImeLabels(const KeyDefinition& key) const -> std::vector<std::string> {
    const auto* typed = std::get_if<TypeKey>(&key.action);
    if (typed == nullptr) {
        return {};
    }
    const auto table = ImeTable(m_ime);
    const auto found = std::ranges::find(table, typed->key, &FixedLabel::key);
    if (found == table.end()) {
        return {};
    }
    if (found->upper.empty()) {
        return {std::string(found->lower)};
    }
    return {std::string(found->lower), std::string(found->upper)};
}

auto KeyboardLayout::AltModifierMask() const -> uint8_t {
    switch (m_alt_key) {
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
    std::ranges::stable_sort(m_keys, [](const KeyDefinition& a, const KeyDefinition& b) -> bool {
        return a.row != b.row ? a.row < b.row : a.x < b.x;
    });
}

auto KeyboardLayout::Load(std::span<const std::string_view> built_in, const std::filesystem::path& user_directory)
    -> std::vector<KeyboardLayout> {
    std::vector<KeyboardLayout> profiles;
    const auto add = [&profiles](KeyboardLayout layout) -> void {
        const auto existing = std::ranges::find(profiles, layout.Name(), &KeyboardLayout::Name);
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
        const auto text = ConfigStore::ReadFile(file);
        if (!text) {
            LOG_ERROR("Could not read keyboard profile {}", file.string());
            continue;
        }
        auto layout = KeyboardLayout::Parse(*text);
        if (!layout) {
            LOG_ERROR("Skipping keyboard profile {}: {}", file.string(), layout.error().Message());
            continue;
        }
        LOG_INFO("Loaded keyboard profile {} from {}", layout->Name(), file.string());
        layout->SetSource(file);
        add(std::move(*layout));
    }
    return profiles;
}

auto KeyboardLayout::UserDirectory() -> std::filesystem::path {
    return ConfigStore::Root() / "keyboards";
}

auto KeyboardLayout::All() -> const std::vector<KeyboardLayout>& {
    auto& loaded = Loaded();
    if (!loaded) {
        loaded = Load(BuiltInProfiles(), UserDirectory());
    }
    return *loaded;
}

auto KeyboardLayout::Save(KeyboardLayout& layout) -> Result<> {
    RETURN_IF_ERROR(layout.Validate());
    std::filesystem::path path = layout.Source();
    const auto existing = path.empty() ? std::nullopt : ConfigStore::ReadFile(path);
    const auto stored = existing ? Parse(*existing) : Result<KeyboardLayout>(Unexpected("missing"));
    if (!stored || stored->Name() != layout.Name()) {
        path = UserDirectory() / std::format("{}.yaml", layout.Name());
    }
    RETURN_IF_ERROR(ConfigStore::WriteFile(path, Yaml::Emit(nlohmann::ordered_json(layout))));
    layout.SetSource(path);
    Reload();
    return {};
}

auto KeyboardLayout::Reload() -> void {
    Loaded().reset();
}

auto KeyboardLayout::IsBuiltIn(std::string_view name) -> bool {
    return std::ranges::any_of(BuiltInProfiles(), [name](std::string_view text) -> bool {
        const auto layout = KeyboardLayout::Parse(text);
        return layout && layout->Name() == name;
    });
}

auto KeyboardLayout::ForLocale(std::span<const KeyboardLayout> profiles, std::string_view locale) -> const KeyboardLayout& {
    const auto tag = LocaleTag(locale);
    const KeyboardLayout* chosen = &profiles.front();
    int best = 0;
    for (const auto& profile : profiles) {
        if (const int match = LocaleMatch(profile, tag); match > 0 && match >= best) {
            best = match;
            chosen = &profile;
        }
    }
    return *chosen;
}

auto KeyboardLayout::Find(std::string_view name) -> const KeyboardLayout& {
    if (name.empty()) {
        return ForLocale(All(), Platform::Locale());
    }
    const auto& profiles = All();
    const auto found = std::ranges::find(profiles, name, &KeyboardLayout::Name);
    if (found == profiles.end()) {
        LOG_WARN("Unknown keyboard profile {}, using {}", name, profiles.front().Name());
        return profiles.front();
    }
    return *found;
}

auto KeyboardLayout::After(std::string_view name) -> const KeyboardLayout& {
    const auto& profiles = All();
    const auto found = std::ranges::find(profiles, name, &KeyboardLayout::Name);
    if (found == profiles.end() || std::next(found) == profiles.end()) {
        return profiles.front();
    }
    return *std::next(found);
}
