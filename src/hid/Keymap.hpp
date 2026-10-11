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
struct xkb_state;
struct wl_seat;
struct wl_keyboard;

struct TranslatedKey {
    VirtualKey key = VirtualKey::Escape;
    uint8_t modifiers = 0;
};

struct XkbContextDeleter {
    auto operator()(xkb_context* context) const -> void;
};

struct XkbKeymapDeleter {
    auto operator()(xkb_keymap* keymap) const -> void;
};

struct XkbStateDeleter {
    auto operator()(xkb_state* state) const -> void;
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
    static auto FromNames(std::string_view layout, std::string_view variant) -> Result<Keymap>;

    [[nodiscard]] auto LabelForKey(VirtualKey key, uint8_t modifier) const -> std::string;
    [[nodiscard]] auto HasAltGr() const -> bool;
    [[nodiscard]] auto KeySym(VirtualKey key, uint8_t modifiers) const -> uint32_t;
    [[nodiscard]] auto Find(char32_t character) const -> std::optional<TranslatedKey>;

  private:
    Keymap(std::unique_ptr<xkb_context, XkbContextDeleter> context, std::unique_ptr<xkb_keymap, XkbKeymapDeleter> keymap);

    std::unique_ptr<xkb_context, XkbContextDeleter> context_;
    std::unique_ptr<xkb_keymap, XkbKeymapDeleter> keymap_;
};
