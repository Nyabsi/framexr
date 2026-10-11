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

#include "Button.hpp"
#include "Error.hpp"
#include "VirtualKey.hpp"

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

struct ActionKey {
    std::vector<ButtonAction> press;
    std::vector<ButtonAction> release;
};

using KeyAction = std::variant<BlankKey, TypeKey, ProfileKey, ActionKey>;

struct KeyStyle {
    std::optional<std::string> bg_color;
    std::optional<std::string> fg_color;
    std::optional<float> corner_radius;

    [[nodiscard]] auto Empty() const -> bool { return !bg_color && !fg_color && !corner_radius; }
};

struct KeyDefinition {
    float x = 0.0f;
    int row = 0;
    float width = 1.0f;
    KeyAction action;
    std::optional<std::vector<std::string>> label;
    std::optional<std::string> shifted_label;
    KeyStyle style;

    [[nodiscard]] auto Right() const -> float { return x + width; }
};

struct KeyboardLayout {
    std::string name;
    float columns = 0.0f;
    int rows = 0;
    std::vector<KeyDefinition> keys;
    AltModifier alt_modifier = AltModifier::None;
    std::optional<bool> auto_labels;
    std::optional<std::string> region;
    std::filesystem::path source;

    static auto Parse(std::string_view text) -> Result<KeyboardLayout>;
    static auto DefaultLabel(std::string_view key) -> std::vector<std::string>;
    static auto Snap(float units) -> float;

    [[nodiscard]] auto Validate() const -> Result<>;
    [[nodiscard]] auto KeyAt(float x, int row) const -> std::optional<size_t>;
    [[nodiscard]] auto Fits(float x, int row, float width, std::optional<size_t> ignore = std::nullopt) const -> bool;
    [[nodiscard]] auto FreeCells() const -> std::vector<std::pair<float, int>>;
    [[nodiscard]] auto Labels(const KeyDefinition& key) const -> std::vector<std::string>;
    [[nodiscard]] auto AltModifierMask() const -> uint8_t;
    auto SortKeys() -> void;
};

class KeyboardProfiles final {
  public:
    static auto All() -> const std::vector<KeyboardLayout>&;
    static auto Find(std::string_view name) -> const KeyboardLayout&;
    static auto After(std::string_view name) -> const KeyboardLayout&;
    static auto Reload() -> void;
    static auto UserDirectory() -> std::filesystem::path;
    static auto IsBuiltIn(std::string_view name) -> bool;
    static auto Load(std::span<const std::string_view> built_in, const std::filesystem::path& user_directory)
        -> std::vector<KeyboardLayout>;
};

auto from_json(const nlohmann::json& json, AltModifier& value) -> void;
auto from_json(const nlohmann::json& json, KeyboardLayout& value) -> void;
auto to_json(nlohmann::ordered_json& json, const KeyboardLayout& value) -> void;
