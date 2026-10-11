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

struct ModularUiConfig {
    float width = 0.0f;
    std::optional<std::array<float, 3>> spawn_pos;
    FormLayout form;
    std::vector<ExtraForm> forms;
    std::vector<ModularElement> elements;

    [[nodiscard]] auto Size() const -> std::array<uint32_t, 2>;
    [[nodiscard]] auto Origin() const -> Eigen::Vector2f;
    [[nodiscard]] auto FormCount() const -> size_t { return forms.size() + 1; }
    [[nodiscard]] auto FormRect(size_t index = 0) const -> CanvasRect;
    [[nodiscard]] auto FormRect(size_t index, const Eigen::Vector2f& origin) const -> CanvasRect;
    [[nodiscard]] auto ToolbarRect() const -> std::optional<CanvasRect>;
    [[nodiscard]] auto ToolbarRect(const Eigen::Vector2f& origin) const -> std::optional<CanvasRect>;
    [[nodiscard]] auto Arrange() const -> std::vector<ModularElement>;
    [[nodiscard]] auto Arrange(const Eigen::Vector2f& origin) const -> std::vector<ModularElement>;
    [[nodiscard]] auto Backgrounds(const Eigen::Vector2f& origin) const -> std::vector<ModularElement>;
    auto RemoveForm(size_t index) -> void;
};

class ModularElements final {
  public:
    static auto Rect(const ModularElement& element) -> const std::array<float, 4>&;
    static auto Rect(ModularElement& element) -> std::array<float, 4>&;
    static auto RegionOf(const ModularElement& element) -> Region;
    static auto SetRegion(ModularElement& element, Region region) -> void;
    static auto FormOf(const ModularElement& element) -> size_t;
    static auto SetForm(ModularElement& element, size_t form) -> void;
    static auto TypeName(const ModularElement& element) -> std::string_view;
};

auto from_json(const nlohmann::json& json, ModularElement& value) -> void;
auto from_json(const nlohmann::json& json, ModularUiConfig& value) -> void;
auto to_json(nlohmann::ordered_json& json, const ModularElement& value) -> void;
auto to_json(nlohmann::ordered_json& json, const ModularUiConfig& value) -> void;

class Modular final {
  public:
    static auto CreateCanvas(const ModularUiConfig& config, AppState& app, float scale = 1.0f) -> Result<std::unique_ptr<Canvas>>;
    static auto Fill(
        Canvas& canvas,
        const ModularUiConfig& config,
        AppState& app,
        float scale,
        const Eigen::Vector2f& origin = Eigen::Vector2f::Zero()
    ) -> void;
    static auto FillArranged(
        Canvas& canvas,
        std::span<const ModularElement> arranged,
        AppState& app,
        float scale,
        const Eigen::Vector2f& origin
    ) -> void;

  private:
    static auto SpriteFromPath(const std::string& path, AppState& app) -> Result<std::shared_ptr<Image>>;
    static auto Build(Canvas& canvas, const ModularElement& element, AppState& app, float scale, const Eigen::Vector2f& origin)
        -> void;
};
