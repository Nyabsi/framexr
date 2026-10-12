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

#include "KeyControl.hpp"

#include <array>
#include <utility>

#include "Hid.hpp"
#include "Keyboard.hpp"
#include "KeyboardState.hpp"
#include "TextInput.hpp"
#include "framexr_config.h"

namespace {

constexpr std::array<uint8_t, 5> kAutoReleaseModifiers{kModShift, kModCtrl, kModAlt, kModSuper, kModMeta};

constexpr float kLatchedOutlineWidth = 3.0f;

auto LatchedColor() -> Eigen::Vector4f {
    return {1.0f, 1.0f, 1.0f, 0.16f};
}

auto LatchedOutline() -> HighlightOutline {
    return {.color = {198.0f / 255.0f, 160.0f / 255.0f, 246.0f / 255.0f, 1.0f}, .width = kLatchedOutlineWidth};
}

} // namespace

KeyControl::KeyControl(std::shared_ptr<KeyboardState> state, VirtualKey key)
    : m_state(std::move(state)),
      m_key(key),
      m_sent(key),
      m_latches(Keyboard::IsLatchingKey(key)) {
}

auto KeyControl::OnPress(PointerMode mode) -> void {
    Keyboard::Click();
    if (mode == PointerMode::Right) {
        m_state->SetModifiers(static_cast<uint8_t>(m_state->Modifiers() | kModShift));
    } else if (mode == PointerMode::Middle) {
        m_state->SetModifiers(static_cast<uint8_t>(m_state->Modifiers() | m_state->AltModifier()));
    }
    m_captured = g_input->FocusedTextInput() != nullptr;
    if (m_captured) {
        const auto input = g_input->FocusedTextInput();
        if (m_latches && !input->WantsRawKeys()) {
            m_latched = !m_latched;
            if (m_key == VirtualKey::Caps) {
                m_state->SetCapsLock(m_latched);
            }
            return;
        }
        input->Key(m_key, m_state->Modifiers(), m_state->Text(m_key, m_state->Modifiers()));
        return;
    }
    m_sent = m_key;
    m_translated = false;
    uint8_t modifiers = m_state->Modifiers();
    if (m_state->Translator() && !m_latches) {
        if (const auto target = m_state->Translator()->Translate(m_key, m_state->Modifiers(), m_state->CapsLock())) {
            m_sent = target->key;
            m_translated = true;
            modifiers = static_cast<uint8_t>((m_state->Modifiers() & ~(kModShift | kModMeta)) | target->modifiers);
        }
    }
    g_hid->SetModifiers(modifiers);
    Keyboard::SendKey(m_sent, true);
    if (m_latches) {
        m_latched = !m_latched;
        if (m_key == VirtualKey::Caps) {
            m_state->SetCapsLock(m_latched);
        }
    }
}

auto KeyControl::OnRelease() -> void {
    if (std::exchange(m_captured, false)) {
        m_state->SetModifiers(0);
        Keyboard::SetModifiers(0);
        m_state->RefreshLetterCase();
        return;
    }
    Keyboard::SendKey(m_sent, false);
    if (m_translated) {
        Keyboard::SetModifiers(m_state->Modifiers());
    }
    for (const uint8_t modifier : kAutoReleaseModifiers) {
        if ((m_state->Modifiers() & modifier) != 0) {
            m_state->SetModifiers(static_cast<uint8_t>(m_state->Modifiers() & static_cast<uint8_t>(~modifier)));
            Keyboard::SetModifiers(m_state->Modifiers());
        }
    }
    m_state->RefreshLetterCase();
}

auto KeyControl::HighlightColor() -> std::optional<Eigen::Vector4f> {
    return m_latched ? std::optional(LatchedColor()) : std::nullopt;
}

auto KeyControl::Outline() -> std::optional<HighlightOutline> {
    return m_latched ? std::optional(LatchedOutline()) : std::nullopt;
}
