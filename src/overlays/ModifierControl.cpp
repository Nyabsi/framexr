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

#include "ModifierControl.hpp"

#include <utility>

#include "Keyboard.hpp"
#include "KeyboardState.hpp"
#include "TextInput.hpp"
#include "framexr_config.h"

namespace {

auto ActiveColor() -> Eigen::Vector4f {
    return {198.0f / 255.0f, 160.0f / 255.0f, 246.0f / 255.0f, 0.5f};
}

} // namespace

ModifierControl::ModifierControl(std::shared_ptr<KeyboardState> state, uint8_t modifier)
    : m_state(std::move(state)),
      m_modifier(modifier) {
}

auto ModifierControl::OnPress(PointerMode) -> void {
    m_captured = g_input->FocusedTextInput() != nullptr && g_input->FocusedTextInput()->WantsRawKeys();
    if (m_captured) {
        Keyboard::Click();
        if (const auto key = VirtualKeys::KeyForModifier(m_modifier)) {
            const auto input = g_input->FocusedTextInput();
            input->Key(*key, m_state->Modifiers(), {});
        }
        return;
    }
    m_sticky = (m_state->Modifiers() & m_modifier) == 0;
    m_state->SetModifiers(static_cast<uint8_t>(m_state->Modifiers() | m_modifier));
    Keyboard::Click();
    Keyboard::SetModifiers(m_state->Modifiers());
    m_state->RefreshLetterCase();
}

auto ModifierControl::OnRelease() -> void {
    if (std::exchange(m_captured, false)) {
        return;
    }
    if (!m_sticky) {
        m_state->SetModifiers(static_cast<uint8_t>(m_state->Modifiers() & static_cast<uint8_t>(~m_modifier)));
        Keyboard::SetModifiers(m_state->Modifiers());
        m_state->RefreshLetterCase();
    }
}

auto ModifierControl::HighlightColor() -> std::optional<Eigen::Vector4f> {
    return (m_state->Modifiers() & m_modifier) != 0 ? std::optional(ActiveColor()) : std::nullopt;
}
