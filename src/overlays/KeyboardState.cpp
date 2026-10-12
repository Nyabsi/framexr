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

#include "KeyboardState.hpp"

#include <cctype>

#include "framexr_config.h"

auto KeyboardState::RefreshLetterCase() -> void {
    const bool upper = m_caps_lock != ((m_modifiers & kModShift) != 0);
    for (const auto& letter : m_letters) {
        letter.label->SetText(upper ? letter.upper : letter.lower);
    }
}

auto KeyboardState::Text(VirtualKey key, uint8_t pressed_modifiers) const -> std::string {
    const uint8_t level = static_cast<uint8_t>((pressed_modifiers & (kModShift | kModMeta)) | (m_caps_lock ? kModCapsLock : 0));
    if (m_typing_keymap) {
        return m_typing_keymap->Text(key, level);
    }
    if (key == VirtualKey::Space) {
        return " ";
    }
    const auto name = VirtualKeys::Name(key);
    if (name.size() != 1 || std::isalpha(static_cast<unsigned char>(name.front())) == 0) {
        return {};
    }
    const bool upper = m_caps_lock != ((pressed_modifiers & kModShift) != 0);
    return std::string(1, static_cast<char>(upper ? name.front() : std::tolower(static_cast<unsigned char>(name.front()))));
}
