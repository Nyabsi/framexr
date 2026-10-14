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

#include "Scancodes.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace {

constexpr uint32_t kEvdevOffset = 8;
constexpr uint32_t kLastDirectEvdev = 83;

constexpr auto kScancodes = std::to_array<std::pair<VirtualKey, uint16_t>>({
    {VirtualKey::Oem102, 0x56},
    {VirtualKey::F11, 0x57},
    {VirtualKey::F12, 0x58},
    {VirtualKey::AbntC1, 0x73},
    {VirtualKey::Katakana, 0x78},
    {VirtualKey::Hiragana, 0x77},
    {VirtualKey::Henkan, 0x79},
    {VirtualKey::Kana, 0x70},
    {VirtualKey::Muhenkan, 0x7B},
    {VirtualKey::KpEnter, kScancodeExtended | 0x1C},
    {VirtualKey::RCtrl, kScancodeExtended | 0x1D},
    {VirtualKey::KpDivide, kScancodeExtended | 0x35},
    {VirtualKey::Print, kScancodeExtended | 0x37},
    {VirtualKey::Meta, kScancodeExtended | 0x38},
    {VirtualKey::Home, kScancodeExtended | 0x47},
    {VirtualKey::Up, kScancodeExtended | 0x48},
    {VirtualKey::Prior, kScancodeExtended | 0x49},
    {VirtualKey::Left, kScancodeExtended | 0x4B},
    {VirtualKey::Right, kScancodeExtended | 0x4D},
    {VirtualKey::End, kScancodeExtended | 0x4F},
    {VirtualKey::Down, kScancodeExtended | 0x50},
    {VirtualKey::Next, kScancodeExtended | 0x51},
    {VirtualKey::Insert, kScancodeExtended | 0x52},
    {VirtualKey::Delete, kScancodeExtended | 0x53},
    {VirtualKey::Xf86AudioMute, kScancodeExtended | 0x20},
    {VirtualKey::Xf86AudioLowerVolume, kScancodeExtended | 0x2E},
    {VirtualKey::Xf86AudioRaiseVolume, kScancodeExtended | 0x30},
    {VirtualKey::AbntC2, 0x7E},
    {VirtualKey::Hangul, 0x72},
    {VirtualKey::Hanja, 0x71},
    {VirtualKey::Yen, 0x7D},
    {VirtualKey::LSuper, kScancodeExtended | 0x5B},
    {VirtualKey::RSuper, kScancodeExtended | 0x5C},
    {VirtualKey::Menu, kScancodeExtended | 0x5D},
    {VirtualKey::Xf86Sleep, kScancodeExtended | 0x5F},
    {VirtualKey::Xf86Mail, kScancodeExtended | 0x6C},
    {VirtualKey::Xf86Favorites, kScancodeExtended | 0x66},
    {VirtualKey::Xf86MyComputer, kScancodeExtended | 0x6B},
    {VirtualKey::Xf86Back, kScancodeExtended | 0x6A},
    {VirtualKey::Xf86Forward, kScancodeExtended | 0x69},
    {VirtualKey::Xf86AudioNext, kScancodeExtended | 0x19},
    {VirtualKey::Xf86AudioPlay, kScancodeExtended | 0x22},
    {VirtualKey::Xf86AudioPrev, kScancodeExtended | 0x10},
    {VirtualKey::Xf86AudioStop, kScancodeExtended | 0x24},
    {VirtualKey::Xf86HomePage, kScancodeExtended | 0x32},
    {VirtualKey::Xf86Reload, kScancodeExtended | 0x67},
    {VirtualKey::Xf86Search, kScancodeExtended | 0x65},
    {VirtualKey::F13, 0x64},
    {VirtualKey::F14, 0x65},
    {VirtualKey::F15, 0x66},
    {VirtualKey::F16, 0x67},
    {VirtualKey::F17, 0x68},
    {VirtualKey::F18, 0x69},
    {VirtualKey::F19, 0x6A},
    {VirtualKey::F20, 0x6B},
    {VirtualKey::F21, 0x6C},
    {VirtualKey::F22, 0x6D},
    {VirtualKey::F23, 0x6E},
    {VirtualKey::F24, 0x76},
});

} // namespace

auto Scancodes::ForKey(VirtualKey key) -> std::optional<uint16_t> {
    const auto code = static_cast<uint32_t>(key);
    if (code > kEvdevOffset && code - kEvdevOffset <= kLastDirectEvdev) {
        return static_cast<uint16_t>(code - kEvdevOffset);
    }
    const auto* found = std::ranges::find(kScancodes, key, &std::pair<VirtualKey, uint16_t>::first);
    if (found == kScancodes.end()) {
        return std::nullopt;
    }
    return found->second;
}
