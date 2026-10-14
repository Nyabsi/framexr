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

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "document/Actions.hpp"
#include "Error.hpp"
#include "ImeScript.hpp"
#include "VirtualKeys.hpp"

constexpr float kKeyStep = 0.25f;

enum class AltModifier : uint8_t {
    None,
    Shift,
    Ctrl,
    Alt,
    Super,
    Meta,
};

struct BlankKey {};

struct TypeKey {
    VirtualKey key = VirtualKey::Escape;
};

struct ProfileKey {};

struct ImeKey {};

struct ActionKey {
    std::vector<ButtonAction> press;
    std::vector<ButtonAction> release;
};

using KeyAction = std::variant<BlankKey, TypeKey, ProfileKey, ImeKey, ActionKey>;

struct FixedLabel {
    VirtualKey key;
    std::string_view lower;
    std::string_view upper;
};

struct KeyStyle {
    std::optional<std::string> bg_color;
    std::optional<std::string> fg_color;
    std::optional<float> corner_radius;
};

struct KeyDefinition {
    float x = 0.0f;
    int row = 0;
    float width = 1.0f;
    KeyAction action;
    std::optional<std::vector<std::string>> label;
    std::optional<std::string> shifted_label;
    KeyStyle style;
};

class KeyboardLayout {
  public:
    [[nodiscard]] auto Name() const -> const std::string& { return m_name; }
    [[nodiscard]] auto Columns() const -> float { return m_columns; }
    [[nodiscard]] auto Rows() const -> int { return m_rows; }
    [[nodiscard]] auto Keys() const -> const std::vector<KeyDefinition>& { return m_keys; }
    [[nodiscard]] auto Keys() -> std::vector<KeyDefinition>& { return m_keys; }
    [[nodiscard]] auto AltKey() const -> AltModifier { return m_alt_key; }
    [[nodiscard]] auto AutoLabels() const -> const std::optional<bool>& { return m_auto_labels; }
    [[nodiscard]] auto AutoLabels() -> std::optional<bool>& { return m_auto_labels; }
    [[nodiscard]] auto Ime() const -> ImeScript { return m_ime; }
    [[nodiscard]] auto Locales() const -> const std::vector<std::string>& { return m_locales; }
    [[nodiscard]] auto Region() const -> const std::optional<std::string>& { return m_region; }
    [[nodiscard]] auto Region() -> std::optional<std::string>& { return m_region; }
    [[nodiscard]] auto Source() const -> const std::filesystem::path& { return m_source; }
    [[nodiscard]] auto Source() -> std::filesystem::path& { return m_source; }

    auto SetName(std::string name) -> void { m_name = std::move(name); }
    auto SetColumns(float columns) -> void { m_columns = columns; }
    auto SetRows(int rows) -> void { m_rows = rows; }
    auto SetAltKey(AltModifier alt_key) -> void { m_alt_key = alt_key; }
    auto SetAutoLabels(std::optional<bool> auto_labels) -> void { m_auto_labels = auto_labels; }
    auto SetIme(ImeScript ime) -> void { m_ime = ime; }
    auto SetLocales(std::vector<std::string> locales) -> void { m_locales = std::move(locales); }
    auto SetRegion(std::optional<std::string> region) -> void { m_region = std::move(region); }
    auto SetSource(std::filesystem::path source) -> void { m_source = std::move(source); }

    static auto Parse(std::string_view text) -> Result<KeyboardLayout>;
    static auto UserDirectory() -> std::filesystem::path;
    static auto IsBuiltIn(std::string_view name) -> bool;
    static auto Load(std::span<const std::string_view> built_in, const std::filesystem::path& user_directory)
        -> std::vector<KeyboardLayout>;
    static auto All() -> const std::vector<KeyboardLayout>&;
    static auto ForLocale(std::span<const KeyboardLayout> profiles, std::string_view locale) -> const KeyboardLayout&;
    static auto Find(std::string_view name) -> const KeyboardLayout&;
    static auto After(std::string_view name) -> const KeyboardLayout&;
    static auto Reload() -> void;
    static auto Save(KeyboardLayout& layout) -> Result<>;
    static auto DefaultLabel(std::string_view key) -> std::vector<std::string>;
    static auto Snap(float units) -> float;
    static auto RightEdge(const KeyDefinition& key) -> float { return key.x + key.width; }

    [[nodiscard]] auto Validate() const -> Result<>;
    [[nodiscard]] auto KeyAt(float x, int row) const -> std::optional<size_t>;
    [[nodiscard]] auto Fits(float x, int row, float width, std::optional<size_t> ignore = std::nullopt) const -> bool;
    [[nodiscard]] auto FreeCells() const -> std::vector<std::pair<float, int>>;
    [[nodiscard]] auto Labels(const KeyDefinition& key) const -> std::vector<std::string>;
    [[nodiscard]] auto ImeLabels(const KeyDefinition& key) const -> std::vector<std::string>;
    [[nodiscard]] auto AltModifierMask() const -> uint8_t;
    auto SortKeys() -> void;

  private:
    std::string m_name;
    float m_columns = 0.0f;
    int m_rows = 0;
    std::vector<KeyDefinition> m_keys;
    AltModifier m_alt_key = AltModifier::None;
    ImeScript m_ime = ImeScript::None;
    std::optional<bool> m_auto_labels;
    std::optional<std::string> m_region;
    std::vector<std::string> m_locales;
    std::filesystem::path m_source;
};

auto from_json(const nlohmann::json& json, AltModifier& value) -> void;
auto from_json(const nlohmann::json& json, ImeScript& value) -> void;
auto from_json(const nlohmann::json& json, KeyboardLayout& value) -> void;
auto to_json(nlohmann::ordered_json& json, const KeyboardLayout& value) -> void;
