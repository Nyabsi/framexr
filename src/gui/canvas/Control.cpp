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

#include "Control.hpp"

auto Control::SetText(std::string_view value) -> void {
    if (text == value) {
        return;
    }
    text = value;
    foreground_dirty = true;
}

auto Control::SetSprite(std::shared_ptr<Image> value) -> void {
    sprite = std::move(value);
    background_dirty = true;
}

auto Control::SetSpriteSt(const Eigen::Vector4f& value) -> void {
    if (sprite_st == value) {
        return;
    }
    sprite_st = value;
    background_dirty = true;
}

auto Control::SetFgColor(const Eigen::Vector4f& value) -> void {
    if (fg_color == value) {
        return;
    }
    fg_color = value;
    foreground_dirty = true;
}

auto Control::Update(AppState&) -> void {
}

auto Control::OnPress(AppState&, PointerMode) -> void {
}

auto Control::OnRelease(AppState&) -> void {
}

auto Control::OnScroll(AppState&, float, float) -> void {
}

auto Control::HighlightColor(AppState&) -> std::optional<Eigen::Vector4f> {
    return std::nullopt;
}

auto Control::Outline(AppState&) -> std::optional<HighlightOutline> {
    return std::nullopt;
}
