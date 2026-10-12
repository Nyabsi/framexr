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

#include "CallbackControl.hpp"

#include <utility>

#include "Pointer.hpp"

CallbackControl::CallbackControl(Click click, Click press, ScrollCallback scroll)
    : m_click(std::move(click)),
      m_press(std::move(press)),
      m_scroll(std::move(scroll)) {
}

auto CallbackControl::OnPress(PointerMode) -> void {
    if (m_press) {
        m_press();
    }
}

auto CallbackControl::OnRelease() -> void {
    if (m_click) {
        m_click();
    }
}

auto CallbackControl::OnScroll(float delta_y, float) -> void {
    if (m_scroll) {
        m_scroll(delta_y);
    }
}
