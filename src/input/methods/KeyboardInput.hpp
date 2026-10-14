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

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "Keymap.hpp"
#include "KeyTranslator.hpp"
#include "VirtualKeys.hpp"
#include "methods/LaserPointer.hpp"

struct KeyPress
{
    VirtualKey sent = VirtualKey::Escape;
    bool translated = false;
    bool captured = false;
    bool sticky = false;
};

class KeyboardInput final
{
  public:
    auto Press(VirtualKey key, PointerMode mode) -> void;
    auto Release(VirtualKey key) -> void;
    auto Reset() -> void;

    [[nodiscard]] auto Modifiers() const -> uint8_t { return m_modifiers; }
    [[nodiscard]] auto Holds(uint8_t modifier) const -> bool { return (m_modifiers & modifier) != 0; }
    [[nodiscard]] auto Latched(VirtualKey key) const -> bool { return m_latched.contains(key); }
    [[nodiscard]] auto CapsLock() const -> bool { return Latched(VirtualKey::Caps); }
    [[nodiscard]] auto Uppercase() const -> bool { return CapsLock() != Holds(kModShift); }
    [[nodiscard]] auto TypingKeymap() const -> const std::shared_ptr<const Keymap>& { return m_typing_keymap; }
    [[nodiscard]] auto Text(VirtualKey key, uint8_t modifiers) const -> std::string;

    auto SetAltModifier(uint8_t alt_modifier) -> void { m_alt_modifier = alt_modifier; }
    auto SetTranslator(std::shared_ptr<const KeyTranslator> translator) -> void { m_translator = std::move(translator); }
    auto SetTypingKeymap(std::shared_ptr<const Keymap> typing_keymap) -> void { m_typing_keymap = std::move(typing_keymap); }

  private:
    auto PressModifier(VirtualKey key, uint8_t modifier) -> void;
    auto ReleaseModifier(uint8_t modifier, const KeyPress& press) -> void;
    auto SetModifiers(uint8_t modifiers) -> void;

    uint8_t m_modifiers = 0;
    uint8_t m_alt_modifier = 0;
    std::set<VirtualKey> m_latched;
    std::map<VirtualKey, KeyPress> m_pressed;
    std::shared_ptr<const KeyTranslator> m_translator;
    std::shared_ptr<const Keymap> m_typing_keymap;
};
