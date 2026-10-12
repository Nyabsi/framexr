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

#include "CallbackControl.hpp"
#include "Canvas.hpp"
#include "Control.hpp"

struct Cell {
    std::string text;
    Click click;
    float weight = 1.0f;
    std::optional<Eigen::Vector4f> color;
    bool selected = false;
    ScrollCallback scroll;
    Click press;
    std::optional<Eigen::Vector4f> text_color;
    bool rim = false;
};

class PanelBuilder final {
  public:
    static constexpr float kPadding = 12.0f;
    static constexpr float kRowHeight = 34.0f;
    static constexpr float kGap = 6.0f;

    PanelBuilder(Canvas& canvas);

    auto Title(std::string_view text) -> void;
    auto Caption(std::string_view text) -> void;
    auto Section(std::string_view text) -> void;
    auto Row(std::vector<Cell> cells, float height = kRowHeight) -> void;
    auto Button(Cell cell) -> void;
    auto Stepper(std::string_view label, std::string_view value, Click minus, Click plus, ScrollCallback scroll) -> void;
    auto Field(std::string_view label, std::string_view value, bool focused, Click focus) -> void;
    auto Swatch(std::string_view label, std::string_view color, Click open) -> void;
    auto Swatches(
        std::span<const std::string> colors,
        std::string_view current,
        const std::function<void(const std::string&)>& pick,
        size_t columns
    ) -> void;
    auto Gap(float height = kGap) -> void;

    [[nodiscard]] auto Y() const -> float { return m_y; }
    [[nodiscard]] auto Target() -> Canvas& { return m_canvas; }
    [[nodiscard]] auto TextWidth(std::string_view text, int font_size) const -> float;
    [[nodiscard]] auto Ellipsize(std::string_view text, int font_size, float width) const -> std::string;
    [[nodiscard]] auto Width() const -> float { return m_width; }

  private:
    auto Label(float x, float y, float w, float h, std::string_view text, int font_size, const Eigen::Vector4f& color, bool centered)
        -> void;
    auto Rim(float x, float y, float w, float h) -> void;

    Canvas& m_canvas;
    float m_y = kPadding;
    float m_width;
    float m_radius;
};
