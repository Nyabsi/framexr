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
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "Button.hpp"
#include "Canvas.hpp"
#include "Error.hpp"
#include "Label.hpp"

class AppState;
class Image;

enum class ListLayout : uint8_t {
    Horizontal,
    Vertical,
};

struct PanelElement {
    std::array<float, 4> rect{};
    float corner_radius = 0.0f;
    std::string bg_color;
};

struct LabelElement {
    std::array<float, 4> rect{};
    float corner_radius = 0.0f;
    int font_size = 0;
    std::string fg_color;
    LabelContent content;
    bool centered = false;
};

struct SpriteElement {
    std::array<float, 4> rect{};
    std::string sprite;
    std::optional<std::array<float, 4>> sprite_st;
};

struct ButtonElement {
    std::array<float, 4> rect{};
    float corner_radius = 0.0f;
    int font_size = 0;
    std::string fg_color;
    std::string bg_color;
    std::string text;
    ButtonData data;
};

struct BatteryListElement {
    std::array<float, 4> rect{};
    float corner_radius = 0.0f;
    int font_size = 0;
    std::string fg_color;
    std::string fg_color_low;
    std::string fg_color_charging;
    float low_threshold = 0.0f;
    size_t num_devices = 0;
    ListLayout layout = ListLayout::Horizontal;
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
    float corner_radius = 0.0f;
    int font_size = 0;
    std::string fg_color;
    std::string bg_color;
    ListLayout layout = ListLayout::Horizontal;
    OverlayListTemplate overlay_template;
};

using ModularElement = std::variant<PanelElement, LabelElement, SpriteElement, ButtonElement, BatteryListElement, OverlayListElement>;

struct ModularUiConfig {
    float width = 0.0f;
    std::array<uint32_t, 2> size{};
    std::optional<std::array<float, 3>> spawn_pos;
    std::vector<ModularElement> elements;
};

auto from_json(const nlohmann::json& json, ModularElement& value) -> void;
auto from_json(const nlohmann::json& json, ModularUiConfig& value) -> void;

class Modular final {
  public:
    static auto CreateCanvas(const std::array<uint32_t, 2>& size, const std::vector<ModularElement>& elements, AppState& app)
        -> Result<std::unique_ptr<Canvas>>;

  private:
    static auto SpriteFromPath(const std::string& path, AppState& app) -> Result<std::shared_ptr<Image>>;
    static auto Build(Canvas& canvas, const ModularElement& element, AppState& app) -> void;
};
