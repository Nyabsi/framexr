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

#include "ProfileControl.hpp"

#include <utility>

#include "Keyboard.hpp"
#include "KeyboardProfiles.hpp"
#include "OverlayContainer.hpp"
#include "framexr_config.h"

ProfileControl::ProfileControl(std::shared_ptr<const Keymap> keymap, std::string current)
    : m_keymap(std::move(keymap)),
      m_current(std::move(current)) {
}

auto ProfileControl::OnPress(PointerMode) -> void {
    Keyboard::Click();
    Keyboard::SwitchProfile(m_keymap, g_keyboard_profiles->After(m_current).Name());
}
