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

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "ButtonAction.hpp"
#include "Control.hpp"
#include "Label.hpp"

enum class ListLayout : uint8_t {
    Horizontal,
    Vertical,
};

enum class Region : uint8_t {
    Form,
    Toolbar,
};

enum class ToolbarPosition : uint8_t {
    Above,
    Below,
};

struct PanelElement {
    std::array<float, 4> rect{};
    std::optional<float> corner_radius;
    std::string bg_color;
    Region region = Region::Form;
    size_t form = 0;
};

struct LabelElement {
    std::array<float, 4> rect{};
    std::optional<float> corner_radius;
    int font_size = 0;
    std::string fg_color;
    LabelContent content;
    bool centered = false;
    Region region = Region::Form;
    size_t form = 0;
};

struct SpriteElement {
    std::array<float, 4> rect{};
    std::string sprite;
    std::optional<std::array<float, 4>> sprite_st;
    Region region = Region::Form;
    size_t form = 0;
};

struct ButtonElement {
    std::array<float, 4> rect{};
    std::optional<float> corner_radius;
    int font_size = 0;
    std::string fg_color;
    std::string bg_color;
    std::string text;
    ButtonData data;
    Region region = Region::Form;
    size_t form = 0;
};

struct BatteryListElement {
    std::array<float, 4> rect{};
    std::optional<float> corner_radius;
    int font_size = 0;
    std::string fg_color;
    std::string fg_color_low;
    std::string fg_color_charging;
    float low_threshold = 0.0f;
    size_t num_devices = 0;
    ListLayout layout = ListLayout::Horizontal;
    Region region = Region::Form;
    size_t form = 0;
};

struct OverlayListTemplate {
    std::optional<OverlayAction> click_down;
    std::optional<OverlayAction> click_up;
    std::optional<OverlayAction> long_click_up;
    std::optional<OverlayAction> right_down;
    std::optional<OverlayAction> right_up;
    std::optional<OverlayAction> long_right_up;
    std::optional<OverlayAction> middle_down;
    std::optional<OverlayAction> middle_up;
    std::optional<OverlayAction> long_middle_up;
    std::optional<OverlayAction> scroll_down;
    std::optional<OverlayAction> scroll_up;
};

struct OverlayListElement {
    std::array<float, 4> rect{};
    std::optional<float> corner_radius;
    int font_size = 0;
    std::string fg_color;
    std::string bg_color;
    ListLayout layout = ListLayout::Horizontal;
    OverlayListTemplate overlay_template;
    Region region = Region::Form;
    size_t form = 0;
};

using ModularElement = std::variant<PanelElement, LabelElement, SpriteElement, ButtonElement, BatteryListElement, OverlayListElement>;

struct ToolbarLayout {
    float height = 40.0f;
    float gap = 0.0f;
    float padding = 2.0f;
    ToolbarPosition position = ToolbarPosition::Below;
};

struct FormLayout {
    float width = 400.0f;
    float height = 160.0f;
    std::optional<ToolbarLayout> toolbar;
    std::optional<std::string> bg_color;
};

struct ExtraForm {
    float x = 0.0f;
    float y = 0.0f;
    float width = 200.0f;
    float height = 120.0f;
    std::optional<std::string> bg_color;
};

class ModularUiConfig {
  public:
    [[nodiscard]] auto Width() const -> float { return m_width; }
    [[nodiscard]] auto SpawnPos() const -> const std::optional<std::array<float, 3>>& { return m_spawn_pos; }
    [[nodiscard]] auto SpawnPos() -> std::optional<std::array<float, 3>>& { return m_spawn_pos; }
    [[nodiscard]] auto Form() const -> const FormLayout& { return m_form; }
    [[nodiscard]] auto Form() -> FormLayout& { return m_form; }
    [[nodiscard]] auto Forms() const -> const std::vector<ExtraForm>& { return m_forms; }
    [[nodiscard]] auto Forms() -> std::vector<ExtraForm>& { return m_forms; }
    [[nodiscard]] auto Elements() const -> const std::vector<ModularElement>& { return m_elements; }
    [[nodiscard]] auto Elements() -> std::vector<ModularElement>& { return m_elements; }

    auto SetWidth(float width) -> void { m_width = width; }
    auto SetForm(FormLayout form) -> void { m_form = std::move(form); }
    auto SetElements(std::vector<ModularElement> elements) -> void { m_elements = std::move(elements); }

    [[nodiscard]] auto Size() const -> std::array<uint32_t, 2>;
    [[nodiscard]] auto Origin() const -> Eigen::Vector2f;
    [[nodiscard]] auto FormCount() const -> size_t { return Forms().size() + 1; }
    [[nodiscard]] auto FormRect(size_t index = 0) const -> CanvasRect;
    [[nodiscard]] auto FormRect(size_t index, const Eigen::Vector2f& origin) const -> CanvasRect;
    [[nodiscard]] auto ToolbarRect() const -> std::optional<CanvasRect>;
    [[nodiscard]] auto ToolbarRect(const Eigen::Vector2f& origin) const -> std::optional<CanvasRect>;
    [[nodiscard]] auto Arrange() const -> std::vector<ModularElement>;
    [[nodiscard]] auto Arrange(const Eigen::Vector2f& origin) const -> std::vector<ModularElement>;
    [[nodiscard]] auto Backgrounds(const Eigen::Vector2f& origin) const -> std::vector<ModularElement>;
    auto RemoveForm(size_t index) -> void;

  private:
    float m_width = 0.0f;
    std::optional<std::array<float, 3>> m_spawn_pos;
    FormLayout m_form;
    std::vector<ExtraForm> m_forms;
    std::vector<ModularElement> m_elements;
};

auto from_json(const nlohmann::json& json, ModularElement& value) -> void;

auto from_json(const nlohmann::json& json, ModularUiConfig& value) -> void;

auto to_json(nlohmann::ordered_json& json, const ModularElement& value) -> void;

auto to_json(nlohmann::ordered_json& json, const ModularUiConfig& value) -> void;
