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

#include "Error.hpp"
#include "VirtualKey.hpp"

struct xkb_context;
struct xkb_keymap;
struct wl_seat;
struct wl_keyboard;

struct TranslatedKey {
    VirtualKey key = VirtualKey::Escape;
    uint8_t modifiers = 0;
};

struct Translation {
    std::wstring text;
    bool dead = false;
};

struct WaylandKeymapQuery {
    wl_seat* seat = nullptr;
    wl_keyboard* keyboard = nullptr;
    xkb_context* context = nullptr;
    xkb_keymap* keymap = nullptr;
};

class Keymap final {
  public:
    static auto FromWayland() -> Result<Keymap>;
    static auto FromX11() -> Result<Keymap>;
    static auto FromWindows() -> Result<Keymap>;
    static auto FromNames(std::string_view layout, std::string_view variant) -> Result<Keymap>;

    [[nodiscard]] auto LabelForKey(VirtualKey key, uint8_t modifier) const -> std::string;
    [[nodiscard]] auto HasAltGr() const -> bool;
    [[nodiscard]] auto KeySym(VirtualKey key, uint8_t modifiers) const -> uint32_t;
    [[nodiscard]] auto Text(VirtualKey key, uint8_t modifiers) const -> std::string;
    [[nodiscard]] auto Find(char32_t character) const -> std::optional<TranslatedKey>;

  private:
#ifdef _WIN32
    explicit Keymap(std::unique_ptr<void, void (*)(void*)> layout);

    static auto UnloadLayout(void* layout) -> void;
    static auto KeepLayout(void* layout) -> void;

    std::unique_ptr<void, void (*)(void*)> m_layout;
#else
    Keymap(std::unique_ptr<xkb_context, void (*)(xkb_context*)> context, std::unique_ptr<xkb_keymap, void (*)(xkb_keymap*)> keymap);

    std::unique_ptr<xkb_context, void (*)(xkb_context*)> m_context;
    std::unique_ptr<xkb_keymap, void (*)(xkb_keymap*)> m_keymap;
#endif
};
