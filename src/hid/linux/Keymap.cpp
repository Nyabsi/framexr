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

#include "Keymap.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <string_view>
#include <utility>

#include <xkbcommon/xkbcommon.h>

#include "Logger.hpp"
#include "framexr_config.h"

#if FRAMEXR_FEATURE_WAYLAND
#include <wayland-client.h>
#endif

#if FRAMEXR_FEATURE_X11
#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon-x11.h>
#endif

namespace {

#if FRAMEXR_FEATURE_WAYLAND

auto OnKeyboardKeymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) -> void {
    auto* query = static_cast<WaylandKeymapQuery*>(data);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }
    auto* mapping = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapping == MAP_FAILED) {
        LOG_ERROR("Could not load keymap: mmap failed");
        LOG_ERROR("Default layout will be used.");
        close(fd);
        return;
    }
    query->keymap = xkb_keymap_new_from_buffer(
        query->context,
        static_cast<const char*>(mapping),
        size - 1,
        XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS
    );
    munmap(mapping, size);
    close(fd);
    if (query->keymap == nullptr) {
        LOG_ERROR("Could not load keymap: no keymap");
        LOG_ERROR("Default layout will be used.");
    }
}

auto OnKeyboardEnter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) -> void {
}

auto OnKeyboardLeave(void*, wl_keyboard*, uint32_t, wl_surface*) -> void {
}

auto OnKeyboardKey(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t) -> void {
}

auto OnKeyboardModifiers(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) -> void {
}

auto OnKeyboardRepeatInfo(void*, wl_keyboard*, int32_t rate, int32_t delay) -> void {
    LOG_DEBUG("WlKeyboard RepeatInfo rate: {}, delay: {}", rate, delay);
}

constexpr wl_keyboard_listener kKeyboardListener{
    .keymap = OnKeyboardKeymap,
    .enter = OnKeyboardEnter,
    .leave = OnKeyboardLeave,
    .key = OnKeyboardKey,
    .modifiers = OnKeyboardModifiers,
    .repeat_info = OnKeyboardRepeatInfo,
};

auto OnSeatCapabilities(void* data, wl_seat* seat, uint32_t capabilities) -> void {
    auto* query = static_cast<WaylandKeymapQuery*>(data);
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0 && query->keyboard == nullptr) {
        query->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(query->keyboard, &kKeyboardListener, query);
    }
}

auto OnSeatName(void*, wl_seat*, const char* name) -> void {
    LOG_DEBUG("Using WlSeat: {}", name);
}

constexpr wl_seat_listener kSeatListener{
    .capabilities = OnSeatCapabilities,
    .name = OnSeatName,
};

auto OnRegistryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) -> void {
    auto* query = static_cast<WaylandKeymapQuery*>(data);
    if (std::string_view(interface) == wl_seat_interface.name && query->seat == nullptr && version >= 4) {
        query->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 9U)));
        wl_seat_add_listener(query->seat, &kSeatListener, query);
    }
}

auto OnRegistryGlobalRemove(void*, wl_registry*, uint32_t) -> void {
}

constexpr wl_registry_listener kRegistryListener{
    .global = OnRegistryGlobal,
    .global_remove = OnRegistryGlobalRemove,
};

#endif

constexpr auto kDeadKeyLabels = std::to_array<std::pair<xkb_keysym_t, std::string_view>>({
    {XKB_KEY_dead_grave, "`"},
    {XKB_KEY_dead_acute, "´"},
    {XKB_KEY_dead_circumflex, "^"},
    {XKB_KEY_dead_tilde, "~"},
    {XKB_KEY_dead_macron, "¯"},
    {XKB_KEY_dead_breve, "˘"},
    {XKB_KEY_dead_abovedot, "˙"},
    {XKB_KEY_dead_diaeresis, "¨"},
    {XKB_KEY_dead_abovering, "˚"},
    {XKB_KEY_dead_doubleacute, "˝"},
    {XKB_KEY_dead_caron, "ˇ"},
    {XKB_KEY_dead_cedilla, "¸"},
    {XKB_KEY_dead_ogonek, "˛"},
});

constexpr auto kTypingModifiers = std::to_array<uint8_t>({0, kModShift, kModMeta, kModShift | kModMeta});
constexpr uint32_t kFirstKeycode = 8;
constexpr uint32_t kLastKeycode = 255;

auto DeadKeyLabel(xkb_keysym_t symbol) -> std::string {
    const auto* found = std::ranges::find(kDeadKeyLabels, symbol, &std::pair<xkb_keysym_t, std::string_view>::first);
    return found == kDeadKeyLabels.end() ? std::string() : std::string(found->second);
}

} // namespace

auto XkbContextDeleter::operator()(xkb_context* context) const -> void {
    xkb_context_unref(context);
}

auto XkbKeymapDeleter::operator()(xkb_keymap* keymap) const -> void {
    xkb_keymap_unref(keymap);
}

auto XkbStateDeleter::operator()(xkb_state* state) const -> void {
    xkb_state_unref(state);
}

Keymap::Keymap(std::unique_ptr<xkb_context, XkbContextDeleter> context, std::unique_ptr<xkb_keymap, XkbKeymapDeleter> keymap)
    : context_(std::move(context)),
      keymap_(std::move(keymap)) {
}

auto Keymap::FromWayland() -> Result<Keymap> {
#if FRAMEXR_FEATURE_WAYLAND
    auto* display = wl_display_connect(nullptr);
    if (display == nullptr) {
        return Unexpected("Could not connect to Wayland display");
    }

    std::unique_ptr<xkb_context, XkbContextDeleter> context(xkb_context_new(XKB_CONTEXT_NO_DEFAULT_INCLUDES));
    WaylandKeymapQuery query{.context = context.get()};

    auto* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &kRegistryListener, &query);
    wl_display_roundtrip(display);
    if (query.seat == nullptr) {
        wl_registry_destroy(registry);
        wl_display_disconnect(display);
        FatalError(wl_seat_interface.name);
    }
    wl_display_roundtrip(display);
    wl_display_roundtrip(display);

    if (query.keyboard != nullptr) {
        wl_keyboard_release(query.keyboard);
    }
    wl_seat_release(query.seat);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);

    if (query.keymap == nullptr) {
        return Unexpected("Could not load keymap");
    }
    return Keymap(std::move(context), std::unique_ptr<xkb_keymap, XkbKeymapDeleter>(query.keymap));
#else
    return Unexpected("Wayland support not enabled.");
#endif
}

auto Keymap::FromX11() -> Result<Keymap> {
#if FRAMEXR_FEATURE_X11
    std::unique_ptr<xkb_context, XkbContextDeleter> context(xkb_context_new(XKB_CONTEXT_NO_FLAGS));
    auto* connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(connection) != 0) {
        xcb_disconnect(connection);
        return Unexpected("Could not connect to X11 display");
    }
    xkb_x11_setup_xkb_extension(
        connection,
        XKB_X11_MIN_MAJOR_XKB_VERSION,
        XKB_X11_MIN_MINOR_XKB_VERSION,
        XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
        nullptr,
        nullptr,
        nullptr,
        nullptr
    );
    const int32_t device_id = xkb_x11_get_core_keyboard_device_id(connection);
    if (device_id == -1) {
        xcb_disconnect(connection);
        return Unexpected("get_core_keyboard_device_id returned -1. Check your XKB installation.");
    }
    auto* keymap = xkb_x11_keymap_new_from_device(context.get(), connection, device_id, XKB_KEYMAP_COMPILE_NO_FLAGS);
    xcb_disconnect(connection);
    if (keymap == nullptr) {
        return Unexpected("Could not load keymap");
    }
    return Keymap(std::move(context), std::unique_ptr<xkb_keymap, XkbKeymapDeleter>(keymap));
#else
    return Unexpected("X11 support not enabled.");
#endif
}

auto Keymap::FromWindows() -> Result<Keymap> {
    return Unexpected("Windows support not enabled.");
}

auto Keymap::FromNames(std::string_view layout, std::string_view variant) -> Result<Keymap> {
    std::unique_ptr<xkb_context, XkbContextDeleter> context(xkb_context_new(XKB_CONTEXT_NO_FLAGS));
    if (!context) {
        return Unexpected("Could not create xkb context");
    }
    const std::string layout_name(layout);
    const std::string variant_name(variant);
    const xkb_rule_names names{
        .rules = nullptr,
        .model = nullptr,
        .layout = layout_name.c_str(),
        .variant = variant_name.empty() ? nullptr : variant_name.c_str(),
        .options = nullptr,
    };
    std::unique_ptr<xkb_keymap, XkbKeymapDeleter> keymap(
        xkb_keymap_new_from_names(context.get(), &names, XKB_KEYMAP_COMPILE_NO_FLAGS)
    );
    if (!keymap) {
        return Unexpected(std::format("Unknown keyboard region '{}'", layout));
    }
    return Keymap(std::move(context), std::move(keymap));
}

auto Keymap::KeySym(VirtualKey key, uint8_t modifiers) const -> uint32_t {
    std::unique_ptr<xkb_state, XkbStateDeleter> state(xkb_state_new(keymap_.get()));
    for (const uint8_t modifier : {kModShift, kModCapsLock, kModMeta}) {
        if ((modifiers & modifier) != 0) {
            if (const auto modifier_key = VirtualKeys::KeyForModifier(modifier)) {
                xkb_state_update_key(state.get(), static_cast<xkb_keycode_t>(*modifier_key), XKB_KEY_DOWN);
            }
        }
    }
    return xkb_state_key_get_one_sym(state.get(), static_cast<xkb_keycode_t>(key));
}

auto Keymap::Text(VirtualKey key, uint8_t modifiers) const -> std::string {
    std::unique_ptr<xkb_state, XkbStateDeleter> state(xkb_state_new(keymap_.get()));
    for (const uint8_t modifier : {kModShift, kModCapsLock, kModMeta}) {
        if ((modifiers & modifier) != 0) {
            if (const auto modifier_key = VirtualKeys::KeyForModifier(modifier)) {
                xkb_state_update_key(state.get(), static_cast<xkb_keycode_t>(*modifier_key), XKB_KEY_DOWN);
            }
        }
    }
    const auto keycode = static_cast<xkb_keycode_t>(key);
    const auto size = xkb_state_key_get_utf8(state.get(), keycode, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::string text(static_cast<size_t>(size) + 1, '\0');
    xkb_state_key_get_utf8(state.get(), keycode, text.data(), text.size());
    text.resize(static_cast<size_t>(size));
    return text;
}

auto Keymap::Find(char32_t character) const -> std::optional<TranslatedKey> {
    const auto wanted = xkb_utf32_to_keysym(static_cast<uint32_t>(character));
    if (wanted == XKB_KEY_NoSymbol) {
        return std::nullopt;
    }
    for (const uint8_t modifiers : kTypingModifiers) {
        for (uint32_t code = kFirstKeycode; code <= kLastKeycode; ++code) {
            const auto key = static_cast<VirtualKey>(code);
            if (!VirtualKeys::ModifierOf(key) && KeySym(key, modifiers) == wanted) {
                return TranslatedKey{.key = key, .modifiers = modifiers};
            }
        }
    }
    return std::nullopt;
}

auto Keymap::LabelForKey(VirtualKey key, uint8_t modifier) const -> std::string {
    std::unique_ptr<xkb_state, XkbStateDeleter> state(xkb_state_new(keymap_.get()));
    if (modifier > 0) {
        if (const auto modifier_key = VirtualKeys::KeyForModifier(modifier)) {
            xkb_state_update_key(state.get(), static_cast<xkb_keycode_t>(*modifier_key), XKB_KEY_DOWN);
        }
    }
    const auto keycode = static_cast<xkb_keycode_t>(key);
    const auto size = xkb_state_key_get_utf8(state.get(), keycode, nullptr, 0);
    if (size <= 0) {
        return DeadKeyLabel(xkb_state_key_get_one_sym(state.get(), keycode));
    }
    std::string label(static_cast<size_t>(size) + 1, '\0');
    xkb_state_key_get_utf8(state.get(), keycode, label.data(), label.size());
    label.resize(static_cast<size_t>(size));
    return label;
}

auto Keymap::HasAltGr() const -> bool {
    std::unique_ptr<xkb_state, XkbStateDeleter> base(xkb_state_new(keymap_.get()));
    std::unique_ptr<xkb_state, XkbStateDeleter> altgr(xkb_state_new(keymap_.get()));
    xkb_state_update_key(altgr.get(), static_cast<xkb_keycode_t>(VirtualKey::Meta), XKB_KEY_DOWN);

    constexpr std::array kDigits{
        VirtualKey::N0,
        VirtualKey::N1,
        VirtualKey::N2,
        VirtualKey::N3,
        VirtualKey::N4,
        VirtualKey::N5,
        VirtualKey::N6,
        VirtualKey::N7,
        VirtualKey::N8,
        VirtualKey::N9,
    };
    for (const auto key : kDigits) {
        const auto keycode = static_cast<xkb_keycode_t>(key);
        if (xkb_state_key_get_one_sym(base.get(), keycode) != xkb_state_key_get_one_sym(altgr.get(), keycode)) {
            return true;
        }
    }
    return false;
}
