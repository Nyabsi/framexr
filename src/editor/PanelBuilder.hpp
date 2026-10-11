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

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Core>

#include "Canvas.hpp"
#include "Control.hpp"

class AppState;

using Click = std::function<void(AppState&)>;
using Scroll = std::function<void(AppState&, float)>;

class EditorColors final {
  public:
    static auto Background() -> Eigen::Vector4f;
    static auto Surface() -> Eigen::Vector4f;
    static auto Accent() -> Eigen::Vector4f;
    static auto Text() -> Eigen::Vector4f;
    static auto Dim() -> Eigen::Vector4f;
    static auto Dark() -> Eigen::Vector4f;
    static auto Danger() -> Eigen::Vector4f;
    static auto OnAccent() -> Eigen::Vector4f;
    static auto Hex(std::string_view hex) -> Eigen::Vector4f;
    static auto Readable(const Eigen::Vector4f& background) -> Eigen::Vector4f;
};

class CallbackControl final : public Control {
  public:
    CallbackControl(Click click, Click press, Scroll scroll);

    auto OnPress(AppState& app, PointerMode mode) -> void override;
    auto OnRelease(AppState& app) -> void override;
    auto OnScroll(AppState& app, float delta_y, float delta_x) -> void override;

  private:
    Click click_;
    Click press_;
    Scroll scroll_;
};

class OutlineControl final : public Control {
  public:
    OutlineControl(std::optional<Eigen::Vector4f> fill, std::optional<HighlightOutline> outline);

    auto HighlightColor(AppState& app) -> std::optional<Eigen::Vector4f> override;
    auto Outline(AppState& app) -> std::optional<HighlightOutline> override;

  private:
    std::optional<Eigen::Vector4f> fill_;
    std::optional<HighlightOutline> outline_;
};

struct Cell {
    std::string text;
    Click click;
    float weight = 1.0f;
    std::optional<Eigen::Vector4f> color;
    bool selected = false;
    Scroll scroll;
    Click press;
    std::optional<Eigen::Vector4f> text_color;
    bool rim = false;
};

class PanelBuilder final {
  public:
    static constexpr float kPadding = 12.0f;
    static constexpr float kRowHeight = 34.0f;
    static constexpr float kGap = 6.0f;

    PanelBuilder(Canvas& canvas, AppState& app);

    auto Title(std::string_view text) -> void;
    auto Caption(std::string_view text) -> void;
    auto Section(std::string_view text) -> void;
    auto Row(std::vector<Cell> cells, float height = kRowHeight) -> void;
    auto Button(Cell cell) -> void;
    auto Stepper(std::string_view label, std::string_view value, Click minus, Click plus, Scroll scroll) -> void;
    auto Field(std::string_view label, std::string_view value, bool focused, Click focus) -> void;
    auto Swatch(std::string_view label, std::string_view color, Click open) -> void;
    auto Swatches(
        std::span<const std::string> colors,
        std::string_view current,
        const std::function<void(AppState&, const std::string&)>& pick,
        size_t columns
    ) -> void;
    auto Gap(float height = kGap) -> void;

    [[nodiscard]] auto Y() const -> float { return y_; }
    [[nodiscard]] auto Target() -> Canvas& { return canvas_; }
    [[nodiscard]] auto TextWidth(std::string_view text, int font_size) const -> float;
    [[nodiscard]] auto Ellipsize(std::string_view text, int font_size, float width) const -> std::string;
    [[nodiscard]] auto Width() const -> float { return width_; }

  private:
    auto Label(float x, float y, float w, float h, std::string_view text, int font_size, const Eigen::Vector4f& color, bool centered)
        -> void;
    auto Rim(float x, float y, float w, float h) -> void;

    Canvas& canvas_;
    AppState& app_;
    float y_ = kPadding;
    float width_;
    float radius_;
};
