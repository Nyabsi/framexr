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
#include <optional>
#include <span>
#include <string_view>

enum class VirtualKey : uint8_t {
    Escape = 9,
    N1 = 10,
    N2 = 11,
    N3 = 12,
    N4 = 13,
    N5 = 14,
    N6 = 15,
    N7 = 16,
    N8 = 17,
    N9 = 18,
    N0 = 19,
    Minus = 20,
    Plus = 21,
    BackSpace = 22,
    Tab = 23,
    Q = 24,
    W = 25,
    E = 26,
    R = 27,
    T = 28,
    Y = 29,
    U = 30,
    I = 31,
    O = 32,
    P = 33,
    Oem4 = 34,
    Oem6 = 35,
    Return = 36,
    LCtrl = 37,
    A = 38,
    S = 39,
    D = 40,
    F = 41,
    G = 42,
    H = 43,
    J = 44,
    K = 45,
    L = 46,
    Oem1 = 47,
    Oem7 = 48,
    Oem3 = 49,
    LShift = 50,
    Oem5 = 51,
    Z = 52,
    X = 53,
    C = 54,
    V = 55,
    B = 56,
    N = 57,
    M = 58,
    Comma = 59,
    Period = 60,
    Oem2 = 61,
    RShift = 62,
    KpMultiply = 63,
    LAlt = 64,
    Space = 65,
    Caps = 66,
    F1 = 67,
    F2 = 68,
    F3 = 69,
    F4 = 70,
    F5 = 71,
    F6 = 72,
    F7 = 73,
    F8 = 74,
    F9 = 75,
    F10 = 76,
    NumLock = 77,
    Scroll = 78,
    Kp7 = 79,
    Kp8 = 80,
    Kp9 = 81,
    KpSubtract = 82,
    Kp4 = 83,
    Kp5 = 84,
    Kp6 = 85,
    KpAdd = 86,
    Kp1 = 87,
    Kp2 = 88,
    Kp3 = 89,
    Kp0 = 90,
    KpDecimal = 91,
    Oem102 = 94,
    F11 = 95,
    F12 = 96,
    AbntC1 = 97,
    Katakana = 98,
    Hiragana = 99,
    Henkan = 100,
    Kana = 101,
    Muhenkan = 102,
    KpEnter = 104,
    RCtrl = 105,
    KpDivide = 106,
    Print = 107,
    Meta = 108,
    Home = 110,
    Up = 111,
    Prior = 112,
    Left = 113,
    Right = 114,
    End = 115,
    Down = 116,
    Next = 117,
    Insert = 118,
    Delete = 119,
    Xf86AudioMute = 121,
    Xf86AudioLowerVolume = 122,
    Xf86AudioRaiseVolume = 123,
    Pause = 127,
    AbntC2 = 129,
    Hangul = 130,
    Hanja = 131,
    LSuper = 133,
    RSuper = 134,
    Menu = 135,
    Help = 146,
    Xf86MenuKB = 147,
    Xf86Sleep = 150,
    Xf86Xfer = 155,
    Xf86Launch1 = 156,
    Xf86Launch2 = 157,
    Xf86WWW = 158,
    Xf86Mail = 163,
    Xf86Favorites = 164,
    Xf86MyComputer = 165,
    Xf86Back = 166,
    Xf86Forward = 167,
    Xf86AudioNext = 171,
    Xf86AudioPlay = 172,
    Xf86AudioPrev = 173,
    Xf86AudioStop = 174,
    Xf86HomePage = 180,
    Xf86Reload = 181,
    F13 = 191,
    F14 = 192,
    F15 = 193,
    F16 = 194,
    F17 = 195,
    F18 = 196,
    F19 = 197,
    F20 = 198,
    F21 = 199,
    F22 = 200,
    F23 = 201,
    F24 = 202,
    Hyper = 207,
    Xf86Launch3 = 208,
    Xf86Launch4 = 209,
    Xf86LaunchB = 210,
    Xf86Search = 225,
};
enum class KeyType : uint8_t {
    Symbol,
    NumPad,
    Other,
};

constexpr uint8_t kModShift = 0x01;
constexpr uint8_t kModCapsLock = 0x02;
constexpr uint8_t kModCtrl = 0x04;
constexpr uint8_t kModAlt = 0x08;
constexpr uint8_t kModNumLock = 0x10;
constexpr uint8_t kModSuper = 0x40;
constexpr uint8_t kModMeta = 0x80;

class VirtualKeys {
  public:
    static auto All() -> std::span<const VirtualKey>;
    static auto Parse(std::string_view name) -> std::optional<VirtualKey>;
    static auto Name(VirtualKey key) -> std::string_view;
    static auto Type(VirtualKey key) -> KeyType;
    static auto ModifierOf(VirtualKey key) -> std::optional<uint8_t>;
    static auto KeyForModifier(uint8_t modifier) -> std::optional<VirtualKey>;
};
