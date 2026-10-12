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
#include <string_view>
#include <vector>

#include "Control.hpp"
#include "Keymap.hpp"
#include "KeyTranslator.hpp"
#include "VirtualKey.hpp"

struct LetterLabel {
    Control* label = nullptr;
    std::string lower;
    std::string upper;
};

class KeyboardState {
  public:
    [[nodiscard]] auto Modifiers() const -> uint8_t { return m_modifiers; }
    [[nodiscard]] auto AltModifier() const -> uint8_t { return m_alt_modifier; }
    [[nodiscard]] auto CapsLock() const -> bool { return m_caps_lock; }
    [[nodiscard]] auto Translator() const -> const std::shared_ptr<const KeyTranslator>& { return m_translator; }
    [[nodiscard]] auto Translator() -> std::shared_ptr<const KeyTranslator>& { return m_translator; }
    [[nodiscard]] auto TypingKeymap() const -> const std::shared_ptr<const Keymap>& { return m_typing_keymap; }
    [[nodiscard]] auto TypingKeymap() -> std::shared_ptr<const Keymap>& { return m_typing_keymap; }
    [[nodiscard]] auto Letters() const -> const std::vector<LetterLabel>& { return m_letters; }
    [[nodiscard]] auto Letters() -> std::vector<LetterLabel>& { return m_letters; }

    auto SetModifiers(uint8_t modifiers) -> void { m_modifiers = modifiers; }
    auto SetAltModifier(uint8_t alt_modifier) -> void { m_alt_modifier = alt_modifier; }
    auto SetCapsLock(bool caps_lock) -> void { m_caps_lock = caps_lock; }
    auto SetTranslator(std::shared_ptr<const KeyTranslator> translator) -> void { m_translator = std::move(translator); }
    auto SetTypingKeymap(std::shared_ptr<const Keymap> typing_keymap) -> void { m_typing_keymap = std::move(typing_keymap); }

    auto RefreshLetterCase() -> void;
    [[nodiscard]] auto Text(VirtualKey key, uint8_t pressed_modifiers) const -> std::string;

  private:
    uint8_t m_modifiers = 0;
    uint8_t m_alt_modifier = 0;
    bool m_caps_lock = false;
    std::shared_ptr<const KeyTranslator> m_translator;
    std::shared_ptr<const Keymap> m_typing_keymap;
    std::vector<LetterLabel> m_letters;
};
