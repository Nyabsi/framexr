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

#include "VirtualKeys.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace {

constexpr auto kVirtualKeyNames = std::to_array<std::pair<VirtualKey, std::string_view>>({
    {VirtualKey::Escape, "Escape"},
    {VirtualKey::N1, "N1"},
    {VirtualKey::N2, "N2"},
    {VirtualKey::N3, "N3"},
    {VirtualKey::N4, "N4"},
    {VirtualKey::N5, "N5"},
    {VirtualKey::N6, "N6"},
    {VirtualKey::N7, "N7"},
    {VirtualKey::N8, "N8"},
    {VirtualKey::N9, "N9"},
    {VirtualKey::N0, "N0"},
    {VirtualKey::Minus, "Minus"},
    {VirtualKey::Plus, "Plus"},
    {VirtualKey::BackSpace, "BackSpace"},
    {VirtualKey::Tab, "Tab"},
    {VirtualKey::Q, "Q"},
    {VirtualKey::W, "W"},
    {VirtualKey::E, "E"},
    {VirtualKey::R, "R"},
    {VirtualKey::T, "T"},
    {VirtualKey::Y, "Y"},
    {VirtualKey::U, "U"},
    {VirtualKey::I, "I"},
    {VirtualKey::O, "O"},
    {VirtualKey::P, "P"},
    {VirtualKey::Oem4, "Oem4"},
    {VirtualKey::Oem6, "Oem6"},
    {VirtualKey::Return, "Return"},
    {VirtualKey::LCtrl, "LCtrl"},
    {VirtualKey::A, "A"},
    {VirtualKey::S, "S"},
    {VirtualKey::D, "D"},
    {VirtualKey::F, "F"},
    {VirtualKey::G, "G"},
    {VirtualKey::H, "H"},
    {VirtualKey::J, "J"},
    {VirtualKey::K, "K"},
    {VirtualKey::L, "L"},
    {VirtualKey::Oem1, "Oem1"},
    {VirtualKey::Oem7, "Oem7"},
    {VirtualKey::Oem3, "Oem3"},
    {VirtualKey::LShift, "LShift"},
    {VirtualKey::Oem5, "Oem5"},
    {VirtualKey::Z, "Z"},
    {VirtualKey::X, "X"},
    {VirtualKey::C, "C"},
    {VirtualKey::V, "V"},
    {VirtualKey::B, "B"},
    {VirtualKey::N, "N"},
    {VirtualKey::M, "M"},
    {VirtualKey::Comma, "Comma"},
    {VirtualKey::Period, "Period"},
    {VirtualKey::Oem2, "Oem2"},
    {VirtualKey::RShift, "RShift"},
    {VirtualKey::KpMultiply, "KP_Multiply"},
    {VirtualKey::LAlt, "LAlt"},
    {VirtualKey::Space, "Space"},
    {VirtualKey::Caps, "Caps"},
    {VirtualKey::F1, "F1"},
    {VirtualKey::F2, "F2"},
    {VirtualKey::F3, "F3"},
    {VirtualKey::F4, "F4"},
    {VirtualKey::F5, "F5"},
    {VirtualKey::F6, "F6"},
    {VirtualKey::F7, "F7"},
    {VirtualKey::F8, "F8"},
    {VirtualKey::F9, "F9"},
    {VirtualKey::F10, "F10"},
    {VirtualKey::NumLock, "NumLock"},
    {VirtualKey::Scroll, "Scroll"},
    {VirtualKey::Kp7, "KP_7"},
    {VirtualKey::Kp8, "KP_8"},
    {VirtualKey::Kp9, "KP_9"},
    {VirtualKey::KpSubtract, "KP_Subtract"},
    {VirtualKey::Kp4, "KP_4"},
    {VirtualKey::Kp5, "KP_5"},
    {VirtualKey::Kp6, "KP_6"},
    {VirtualKey::KpAdd, "KP_Add"},
    {VirtualKey::Kp1, "KP_1"},
    {VirtualKey::Kp2, "KP_2"},
    {VirtualKey::Kp3, "KP_3"},
    {VirtualKey::Kp0, "KP_0"},
    {VirtualKey::KpDecimal, "KP_Decimal"},
    {VirtualKey::Oem102, "Oem102"},
    {VirtualKey::F11, "F11"},
    {VirtualKey::F12, "F12"},
    {VirtualKey::AbntC1, "AbntC1"},
    {VirtualKey::Katakana, "Katakana"},
    {VirtualKey::Hiragana, "Hiragana"},
    {VirtualKey::Henkan, "Henkan"},
    {VirtualKey::Kana, "Kana"},
    {VirtualKey::Muhenkan, "Muhenkan"},
    {VirtualKey::KpEnter, "KP_Enter"},
    {VirtualKey::RCtrl, "RCtrl"},
    {VirtualKey::KpDivide, "KP_Divide"},
    {VirtualKey::Print, "Print"},
    {VirtualKey::Meta, "Meta"},
    {VirtualKey::Home, "Home"},
    {VirtualKey::Up, "Up"},
    {VirtualKey::Prior, "Prior"},
    {VirtualKey::Left, "Left"},
    {VirtualKey::Right, "Right"},
    {VirtualKey::End, "End"},
    {VirtualKey::Down, "Down"},
    {VirtualKey::Next, "Next"},
    {VirtualKey::Insert, "Insert"},
    {VirtualKey::Delete, "Delete"},
    {VirtualKey::Xf86AudioMute, "XF86AudioMute"},
    {VirtualKey::Xf86AudioLowerVolume, "XF86AudioLowerVolume"},
    {VirtualKey::Xf86AudioRaiseVolume, "XF86AudioRaiseVolume"},
    {VirtualKey::Pause, "Pause"},
    {VirtualKey::AbntC2, "AbntC2"},
    {VirtualKey::Hangul, "Hangul"},
    {VirtualKey::Hanja, "Hanja"},
    {VirtualKey::Yen, "Yen"},
    {VirtualKey::LSuper, "LSuper"},
    {VirtualKey::RSuper, "RSuper"},
    {VirtualKey::Menu, "Menu"},
    {VirtualKey::Help, "Help"},
    {VirtualKey::Xf86MenuKB, "XF86MenuKB"},
    {VirtualKey::Xf86Sleep, "XF86Sleep"},
    {VirtualKey::Xf86Xfer, "XF86Xfer"},
    {VirtualKey::Xf86Launch1, "XF86Launch1"},
    {VirtualKey::Xf86Launch2, "XF86Launch2"},
    {VirtualKey::Xf86WWW, "XF86WWW"},
    {VirtualKey::Xf86Mail, "XF86Mail"},
    {VirtualKey::Xf86Favorites, "XF86Favorites"},
    {VirtualKey::Xf86MyComputer, "XF86MyComputer"},
    {VirtualKey::Xf86Back, "XF86Back"},
    {VirtualKey::Xf86Forward, "XF86Forward"},
    {VirtualKey::Xf86AudioNext, "XF86AudioNext"},
    {VirtualKey::Xf86AudioPlay, "XF86AudioPlay"},
    {VirtualKey::Xf86AudioPrev, "XF86AudioPrev"},
    {VirtualKey::Xf86AudioStop, "XF86AudioStop"},
    {VirtualKey::Xf86HomePage, "XF86HomePage"},
    {VirtualKey::Xf86Reload, "XF86Reload"},
    {VirtualKey::F13, "F13"},
    {VirtualKey::F14, "F14"},
    {VirtualKey::F15, "F15"},
    {VirtualKey::F16, "F16"},
    {VirtualKey::F17, "F17"},
    {VirtualKey::F18, "F18"},
    {VirtualKey::F19, "F19"},
    {VirtualKey::F20, "F20"},
    {VirtualKey::F21, "F21"},
    {VirtualKey::F22, "F22"},
    {VirtualKey::F23, "F23"},
    {VirtualKey::F24, "F24"},
    {VirtualKey::Hyper, "Hyper"},
    {VirtualKey::Xf86Launch3, "XF86Launch3"},
    {VirtualKey::Xf86Launch4, "XF86Launch4"},
    {VirtualKey::Xf86LaunchB, "XF86LaunchB"},
    {VirtualKey::Xf86Search, "XF86Search"},
});
constexpr auto kAllKeys = []->std::array<VirtualKey, kVirtualKeyNames.size()> {
    std::array<VirtualKey, kVirtualKeyNames.size()> keys;
    for (size_t i = 0; i < keys.size(); ++i) {
        keys[i] = kVirtualKeyNames[i].first;
    }
    return keys;
}
();

constexpr auto kKeyModifiers = std::to_array<std::pair<VirtualKey, uint8_t>>({
    {VirtualKey::LShift, kModShift},
    {VirtualKey::RShift, kModShift},
    {VirtualKey::Caps, kModCapsLock},
    {VirtualKey::LCtrl, kModCtrl},
    {VirtualKey::RCtrl, kModCtrl},
    {VirtualKey::LAlt, kModAlt},
    {VirtualKey::NumLock, kModNumLock},
    {VirtualKey::LSuper, kModSuper},
    {VirtualKey::RSuper, kModSuper},
    {VirtualKey::Meta, kModMeta},
});

auto Between(VirtualKey key, VirtualKey first, VirtualKey last) -> bool {
    return key >= first && key <= last;
}

} // namespace

auto VirtualKeys::All() -> std::span<const VirtualKey> {
    return kAllKeys;
}

auto VirtualKeys::Parse(std::string_view name) -> std::optional<VirtualKey> {
    const auto* entry = std::ranges::find(kVirtualKeyNames, name, &std::pair<VirtualKey, std::string_view>::second);
    if (entry == kVirtualKeyNames.end()) {
        return std::nullopt;
    }
    return entry->first;
}

auto VirtualKeys::Name(VirtualKey key) -> std::string_view {
    const auto* entry = std::ranges::find(kVirtualKeyNames, key, &std::pair<VirtualKey, std::string_view>::first);
    return entry == kVirtualKeyNames.end() ? std::string_view{} : entry->second;
}

auto VirtualKeys::Type(VirtualKey key) -> KeyType {
    if (Between(key, VirtualKey::N1, VirtualKey::Plus) || Between(key, VirtualKey::Q, VirtualKey::Oem6)
        || Between(key, VirtualKey::A, VirtualKey::Oem3) || Between(key, VirtualKey::Oem5, VirtualKey::Oem2)
        || key == VirtualKey::Oem102 || key == VirtualKey::AbntC1 || key == VirtualKey::Yen) {
        return KeyType::Symbol;
    }
    if (Between(key, VirtualKey::Kp7, VirtualKey::Kp0) && key != VirtualKey::KpSubtract && key != VirtualKey::KpAdd) {
        return KeyType::NumPad;
    }
    return KeyType::Other;
}

auto VirtualKeys::Chord(std::span<const VirtualKey> keys) -> std::string {
    std::string text;
    for (const auto key : keys) {
        if (!text.empty()) {
            text += " + ";
        }
        text += Name(key);
    }
    return text.empty() ? std::string("(none)") : text;
}

auto VirtualKeys::Latches(VirtualKey key) -> bool {
    return key == VirtualKey::Caps || key == VirtualKey::NumLock || key == VirtualKey::Scroll;
}

auto VirtualKeys::ModifierOf(VirtualKey key) -> std::optional<uint8_t> {
    const auto* entry = std::ranges::find(kKeyModifiers, key, &std::pair<VirtualKey, uint8_t>::first);
    if (entry == kKeyModifiers.end()) {
        return std::nullopt;
    }
    return entry->second;
}

auto VirtualKeys::KeyForModifier(uint8_t modifier) -> std::optional<VirtualKey> {
    const auto* entry = std::ranges::find(kKeyModifiers, modifier, &std::pair<VirtualKey, uint8_t>::second);
    if (entry == kKeyModifiers.end()) {
        return std::nullopt;
    }
    return entry->first;
}
