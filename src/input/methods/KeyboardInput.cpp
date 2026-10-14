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

#include "methods/KeyboardInput.hpp"

#include <algorithm>
#include <array>
#include <cctype>

#include "HidProvider.hpp"
#include "Interaction.hpp"
#include "TextInput.hpp"

namespace
{

constexpr auto kOneShotModifiers = std::to_array<uint8_t>({kModShift, kModCtrl, kModAlt, kModSuper, kModMeta});

} // namespace

auto KeyboardInput::Press(VirtualKey key, PointerMode mode) -> void
{
    const bool latches = VirtualKeys::Latches(key);
    if (const auto modifier = VirtualKeys::ModifierOf(key); modifier && !latches)
    {
        PressModifier(key, *modifier);
        return;
    }
    if (mode == PointerMode::Right)
    {
        m_modifiers |= kModShift;
    }
    else if (mode == PointerMode::Middle)
    {
        m_modifiers |= m_alt_modifier;
    }

    auto& press = m_pressed[key];
    press = {.sent = key};
    const auto focused = g_interaction->FocusedTextInput();
    press.captured = focused != nullptr;
    if (focused && !(latches && !focused->WantsRawKeys()))
    {
        focused->Key(key, m_modifiers, Text(key, m_modifiers));
        return;
    }
    if (latches && m_latched.erase(key) == 0)
    {
        m_latched.insert(key);
    }
    if (focused)
    {
        return;
    }

    uint8_t modifiers = m_modifiers;
    if (m_translator && !latches)
    {
        if (const auto target = m_translator->Translate(key, m_modifiers, CapsLock()))
        {
            press.sent = target->key;
            press.translated = true;
            modifiers = static_cast<uint8_t>((m_modifiers & ~(kModShift | kModMeta)) | target->modifiers);
        }
    }
    g_hid->SetModifiers(modifiers);
    g_hid->SendKey(press.sent, true);
}

auto KeyboardInput::Release(VirtualKey key) -> void
{
    const auto found = m_pressed.find(key);
    if (found == m_pressed.end())
    {
        return;
    }
    const KeyPress press = found->second;
    m_pressed.erase(found);
    if (const auto modifier = VirtualKeys::ModifierOf(key); modifier && !VirtualKeys::Latches(key))
    {
        ReleaseModifier(*modifier, press);
        return;
    }
    if (press.captured)
    {
        SetModifiers(0);
        return;
    }
    g_hid->SendKey(press.sent, false);
    if (press.translated)
    {
        g_hid->SetModifiers(m_modifiers);
    }
    for (const uint8_t modifier : kOneShotModifiers)
    {
        if (Holds(modifier))
        {
            SetModifiers(static_cast<uint8_t>(m_modifiers & ~modifier));
        }
    }
}

auto KeyboardInput::Reset() -> void
{
    if (m_modifiers != 0)
    {
        SetModifiers(0);
    }
}

auto KeyboardInput::PressModifier(VirtualKey key, uint8_t modifier) -> void
{
    auto& press = m_pressed[key];
    press = {.sent = key};
    const auto focused = g_interaction->FocusedTextInput();
    press.captured = focused && focused->WantsRawKeys();
    if (press.captured)
    {
        focused->Key(key, m_modifiers, {});
        return;
    }
    press.sticky = !Holds(modifier);
    SetModifiers(m_modifiers | modifier);
}

auto KeyboardInput::ReleaseModifier(uint8_t modifier, const KeyPress& press) -> void
{
    if (!press.captured && !press.sticky)
    {
        SetModifiers(static_cast<uint8_t>(m_modifiers & ~modifier));
    }
}

auto KeyboardInput::SetModifiers(uint8_t modifiers) -> void
{
    m_modifiers = modifiers;
    g_hid->SetModifiers(m_modifiers);
}

auto KeyboardInput::Text(VirtualKey key, uint8_t modifiers) const -> std::string
{
    const auto level = static_cast<uint8_t>((modifiers & (kModShift | kModMeta)) | (CapsLock() ? kModCapsLock : 0));
    if (m_typing_keymap)
    {
        return m_typing_keymap->Text(key, level);
    }
    if (key == VirtualKey::Space)
    {
        return " ";
    }
    const auto name = VirtualKeys::Name(key);
    if (name.size() != 1 || std::isalpha(static_cast<unsigned char>(name.front())) == 0)
    {
        return {};
    }
    const bool upper = CapsLock() != ((modifiers & kModShift) != 0);
    return std::string(1, static_cast<char>(upper ? name.front() : std::tolower(static_cast<unsigned char>(name.front()))));
}
