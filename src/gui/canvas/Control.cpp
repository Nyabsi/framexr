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
    if (m_text == value) {
        return;
    }
    m_text = value;
    m_foreground_dirty = true;
}

auto Control::SetSprite(std::shared_ptr<Image> value) -> void {
    m_sprite = std::move(value);
    m_background_dirty = true;
}

auto Control::SetSpriteSt(const Eigen::Vector4f& value) -> void {
    if (m_sprite_st == value) {
        return;
    }
    m_sprite_st = value;
    m_background_dirty = true;
}

auto Control::SetFgColor(const Eigen::Vector4f& value) -> void {
    if (m_fg_color == value) {
        return;
    }
    m_fg_color = value;
    m_foreground_dirty = true;
}

auto Control::Update() -> void {
}

auto Control::OnPress(PointerMode) -> void {
}

auto Control::OnRelease() -> void {
}

auto Control::OnScroll(float, float) -> void {
}

auto Control::HighlightColor() -> std::optional<Eigen::Vector4f> {
    return std::nullopt;
}

auto Control::Outline() -> std::optional<HighlightOutline> {
    return std::nullopt;
}
