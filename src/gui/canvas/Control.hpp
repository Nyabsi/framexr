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

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <Eigen/Core>

#include "Image.hpp"
#include "Input.hpp"

struct CanvasRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

enum class BackgroundStyle : uint8_t {
    None,
    RoundedRect,
    Sprite,
};

enum class ForegroundStyle : uint8_t {
    None,
    Text,
    TextCentered,
};

enum class HighlightStyle : uint8_t {
    None,
    RoundedRect,
    Sprite,
};

struct HighlightOutline {
    Eigen::Vector4f color = Eigen::Vector4f::Zero();
    float width = 0.0f;
};

class Control {
  public:
    virtual ~Control() = default;

    [[nodiscard]] auto Rect() const -> const CanvasRect& { return m_rect; }
    [[nodiscard]] auto Rect() -> CanvasRect& { return m_rect; }
    [[nodiscard]] auto CornerRadius() const -> float { return m_corner_radius; }
    [[nodiscard]] auto FgColor() const -> const Eigen::Vector4f& { return m_fg_color; }
    [[nodiscard]] auto FgColor() -> Eigen::Vector4f& { return m_fg_color; }
    [[nodiscard]] auto BgColor() const -> const Eigen::Vector4f& { return m_bg_color; }
    [[nodiscard]] auto BgColor() -> Eigen::Vector4f& { return m_bg_color; }
    [[nodiscard]] auto Text() const -> const std::string& { return m_text; }
    [[nodiscard]] auto Text() -> std::string& { return m_text; }
    [[nodiscard]] auto FontSize() const -> int { return m_font_size; }
    [[nodiscard]] auto Sprite() const -> const std::shared_ptr<Image>& { return m_sprite; }
    [[nodiscard]] auto Sprite() -> std::shared_ptr<Image>& { return m_sprite; }
    [[nodiscard]] auto SpriteSt() const -> const Eigen::Vector4f& { return m_sprite_st; }
    [[nodiscard]] auto SpriteSt() -> Eigen::Vector4f& { return m_sprite_st; }
    [[nodiscard]] auto Background() const -> const BackgroundStyle& { return m_background; }
    [[nodiscard]] auto Background() -> BackgroundStyle& { return m_background; }
    [[nodiscard]] auto Foreground() const -> const ForegroundStyle& { return m_foreground; }
    [[nodiscard]] auto Foreground() -> ForegroundStyle& { return m_foreground; }
    [[nodiscard]] auto Highlight() const -> const HighlightStyle& { return m_highlight; }
    [[nodiscard]] auto Highlight() -> HighlightStyle& { return m_highlight; }
    [[nodiscard]] auto BackgroundDirty() const -> bool { return m_background_dirty; }
    [[nodiscard]] auto ForegroundDirty() const -> bool { return m_foreground_dirty; }

    auto SetRect(CanvasRect rect) -> void { m_rect = rect; }
    auto SetCornerRadius(float corner_radius) -> void { m_corner_radius = corner_radius; }
    auto SetBgColor(Eigen::Vector4f bg_color) -> void { m_bg_color = std::move(bg_color); }
    auto SetFontSize(int font_size) -> void { m_font_size = font_size; }
    auto SetBackground(BackgroundStyle background) -> void { m_background = background; }
    auto SetForeground(ForegroundStyle foreground) -> void { m_foreground = foreground; }
    auto SetHighlight(HighlightStyle highlight) -> void { m_highlight = highlight; }
    auto SetBackgroundDirty(bool background_dirty) -> void { m_background_dirty = background_dirty; }
    auto SetForegroundDirty(bool foreground_dirty) -> void { m_foreground_dirty = foreground_dirty; }
    auto SetText(std::string_view value) -> void;
    auto SetSprite(std::shared_ptr<Image> value) -> void;
    auto SetSpriteSt(const Eigen::Vector4f& value) -> void;
    auto SetFgColor(const Eigen::Vector4f& value) -> void;

    virtual auto Update() -> void;
    virtual auto OnPress(PointerMode mode) -> void;
    virtual auto OnRelease() -> void;
    virtual auto OnScroll(float delta_y, float delta_x) -> void;
    virtual auto HighlightColor() -> std::optional<Eigen::Vector4f>;
    virtual auto Outline() -> std::optional<HighlightOutline>;

  private:
    CanvasRect m_rect;
    float m_corner_radius = 0.0f;
    Eigen::Vector4f m_fg_color = Eigen::Vector4f::Ones();
    Eigen::Vector4f m_bg_color = Eigen::Vector4f::Zero();
    std::string m_text;
    int m_font_size = 24;
    std::shared_ptr<Image> m_sprite;
    Eigen::Vector4f m_sprite_st{1.0f, 1.0f, 0.0f, 0.0f};
    BackgroundStyle m_background = BackgroundStyle::None;
    ForegroundStyle m_foreground = ForegroundStyle::None;
    HighlightStyle m_highlight = HighlightStyle::None;
    bool m_background_dirty = true;
    bool m_foreground_dirty = true;
};
