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

class AppState;

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

    CanvasRect rect;
    float corner_radius = 0.0f;
    Eigen::Vector4f fg_color = Eigen::Vector4f::Ones();
    Eigen::Vector4f bg_color = Eigen::Vector4f::Zero();
    std::string text;
    int font_size = 24;
    std::shared_ptr<Image> sprite;
    Eigen::Vector4f sprite_st{1.0f, 1.0f, 0.0f, 0.0f};
    BackgroundStyle background = BackgroundStyle::None;
    ForegroundStyle foreground = ForegroundStyle::None;
    HighlightStyle highlight = HighlightStyle::None;
    bool background_dirty = true;
    bool foreground_dirty = true;

    auto SetText(std::string_view value) -> void;
    auto SetSprite(std::shared_ptr<Image> value) -> void;
    auto SetSpriteSt(const Eigen::Vector4f& value) -> void;
    auto SetFgColor(const Eigen::Vector4f& value) -> void;

    virtual auto Update(AppState& app) -> void;
    virtual auto OnPress(AppState& app, PointerMode mode) -> void;
    virtual auto OnRelease(AppState& app) -> void;
    virtual auto OnScroll(AppState& app, float delta_y, float delta_x) -> void;
    virtual auto HighlightColor(AppState& app) -> std::optional<Eigen::Vector4f>;
    virtual auto Outline(AppState& app) -> std::optional<HighlightOutline>;
};
