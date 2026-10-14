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

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <Eigen/Core>

#include "FormComponent.hpp"

class Editor;

class Image;

struct ColorTarget
{
    std::string title;
    std::function<std::string()> get;
    std::function<void(const std::string&)> set;
    std::function<void()> reset;
    bool tokens = true;
};

enum class ColorDrag : uint8_t
{
    None,
    Wheel,
    Brightness,
};

class ColorDialog final
{
  public:
    [[nodiscard]] auto Target() const -> const std::optional<ColorTarget>& { return m_target; }
    [[nodiscard]] auto Hsv() const -> const std::array<float, 3>& { return m_hsv; }
    [[nodiscard]] auto Recent() const -> const std::deque<std::string>& { return m_recent; }
    [[nodiscard]] auto WheelRect() const -> const CanvasRect& { return m_wheel_rect; }
    [[nodiscard]] auto BarRect() const -> const CanvasRect& { return m_bar_rect; }
    [[nodiscard]] auto Dragging() const -> bool { return m_drag != ColorDrag::None; }

    auto Open(ColorTarget target) -> void;
    auto SetHsv(const std::array<float, 3>& hsv) -> void { m_hsv = hsv; }
    auto SetRects(const CanvasRect& wheel, const CanvasRect& bar) -> void;
    auto WheelImage() -> std::shared_ptr<Image>;
    auto BarImage() -> std::shared_ptr<Image>;
    auto Pick(const std::string& color) -> void;
    auto Pointer(Editor& editor, const Eigen::Vector2f& point, bool pressed) -> bool;
    auto Hover(Editor& editor, const Eigen::Vector2f& point) -> bool;

  private:
    auto Drag(Editor& editor, const Eigen::Vector2f& point) -> void;
    auto Remember(const std::string& color) -> void;

    std::optional<ColorTarget> m_target;
    std::array<float, 3> m_hsv{0.0f, 0.0f, 1.0f};
    ColorDrag m_drag = ColorDrag::None;
    CanvasRect m_wheel_rect;
    CanvasRect m_bar_rect;
    std::shared_ptr<Image> m_wheel;
    float m_wheel_value = -1.0f;
    std::shared_ptr<Image> m_bar;
    Eigen::Vector2f m_bar_key = Eigen::Vector2f::Constant(-1.0f);
    std::deque<std::string> m_recent;
};
