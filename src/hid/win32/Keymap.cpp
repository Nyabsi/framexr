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

#include "Keymap.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "Scancodes.hpp"

namespace {

constexpr auto kTypingModifiers = std::to_array<uint8_t>({0, kModShift, kModMeta, kModShift | kModMeta});
constexpr uint32_t kFirstKeycode = 8;
constexpr uint32_t kLastKeycode = 255;
constexpr uint32_t kDeadKeyBit = 0x10000000;
constexpr UINT kKeepKeyboardState = 0x4;

constexpr auto kLayouts = std::to_array<std::pair<std::string_view, std::wstring_view>>({
    {"us", L"00000409"}, {"us(dvorak)", L"00010409"}, {"us(intl)", L"00020409"}, {"gb", L"00000809"}, {"fi", L"0000040B"},
    {"se", L"0000041D"}, {"no", L"00000414"},         {"dk", L"00000406"},       {"is", L"0000040F"}, {"de", L"00000407"},
    {"ch", L"00000807"}, {"ch(fr)", L"0000100C"},     {"at", L"00000C07"},       {"fr", L"0000040C"}, {"be", L"0000080C"},
    {"nl", L"00000413"}, {"es", L"0000040A"},         {"pt", L"00000816"},       {"br", L"00000416"}, {"it", L"00000410"},
    {"pl", L"00000415"}, {"cz", L"00000405"},         {"sk", L"0000041B"},       {"hu", L"0000040E"}, {"ro", L"00010418"},
    {"ee", L"00000425"}, {"lv", L"00000426"},         {"lt", L"00000427"},       {"ru", L"00000419"}, {"ua", L"00000422"},
    {"gr", L"00000408"}, {"tr", L"0000041F"},         {"il", L"0000040D"},       {"jp", L"00000411"}, {"kr", L"00000412"},
    {"cn", L"00000804"}, {"tw", L"00000404"},
});

auto Narrow(std::wstring_view text) -> std::string {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string narrow(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), narrow.data(), size, nullptr, nullptr);
    return narrow;
}

auto FirstCodepoint(std::wstring_view text) -> uint32_t {
    if (text.empty()) {
        return 0;
    }
    const auto high = static_cast<uint32_t>(text[0]);
    if (high >= 0xD800 && high < 0xDC00 && text.size() > 1) {
        return 0x10000 + ((high - 0xD800) << 10) + (static_cast<uint32_t>(text[1]) - 0xDC00);
    }
    return high;
}

struct Translation {
    std::wstring text;
    bool dead = false;
};

auto KeyboardState(uint8_t modifiers) -> std::array<BYTE, 256> {
    std::array<BYTE, 256> state{};
    constexpr BYTE kDown = 0x80;
    if ((modifiers & kModShift) != 0) {
        state[VK_SHIFT] = kDown;
        state[VK_LSHIFT] = kDown;
    }
    if ((modifiers & kModCapsLock) != 0) {
        state[VK_CAPITAL] = 0x01;
    }
    if ((modifiers & kModMeta) != 0) {
        state[VK_CONTROL] = kDown;
        state[VK_LCONTROL] = kDown;
        state[VK_MENU] = kDown;
        state[VK_RMENU] = kDown;
    }
    return state;
}

auto Translate(HKL layout, VirtualKey key, uint8_t modifiers) -> Translation {
    const auto scancode = Scancodes::ForKey(key);
    if (!scancode) {
        return {};
    }
    const UINT virtual_key = MapVirtualKeyExW(*scancode, MAPVK_VSC_TO_VK_EX, layout);
    if (virtual_key == 0) {
        return {};
    }
    const auto state = KeyboardState(modifiers);
    std::array<wchar_t, 8> buffer{};
    const int count = ToUnicodeEx(
        virtual_key,
        *scancode & 0xFF,
        state.data(),
        buffer.data(),
        static_cast<int>(buffer.size()),
        kKeepKeyboardState,
        layout
    );
    if (count < 0) {
        const auto empty = KeyboardState(0);
        std::array<wchar_t, 8> flush{};
        ToUnicodeEx(
            VK_SPACE,
            MapVirtualKeyExW(VK_SPACE, MAPVK_VK_TO_VSC, layout),
            empty.data(),
            flush.data(),
            8,
            kKeepKeyboardState,
            layout
        );
        return {.text = std::wstring(1, buffer[0]), .dead = true};
    }
    std::wstring text(buffer.data(), static_cast<size_t>(std::max(count, 0)));
    if (std::ranges::any_of(text, [](wchar_t character) -> bool { return character < 0x20 || character == 0x7F; })) {
        return {};
    }
    return {.text = std::move(text)};
}

} // namespace

auto KeyboardLayoutDeleter::operator()(void* layout) const -> void {
    if (owned) {
        UnloadKeyboardLayout(static_cast<HKL>(layout));
    }
}

Keymap::Keymap(std::unique_ptr<void, KeyboardLayoutDeleter> layout) : layout_(std::move(layout)) {
}

auto Keymap::FromWayland() -> Result<Keymap> {
    return Unexpected("Wayland support not enabled.");
}

auto Keymap::FromX11() -> Result<Keymap> {
    return Unexpected("X11 support not enabled.");
}

auto Keymap::FromWindows() -> Result<Keymap> {
    HWND foreground = GetForegroundWindow();
    const DWORD thread = foreground != nullptr ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    HKL layout = GetKeyboardLayout(thread);
    if (layout == nullptr) {
        return Unexpected("Could not read the active keyboard layout");
    }
    return Keymap(std::unique_ptr<void, KeyboardLayoutDeleter>(layout, KeyboardLayoutDeleter{.owned = false}));
}

auto Keymap::FromNames(std::string_view layout, std::string_view variant) -> Result<Keymap> {
    const auto name = variant.empty() ? std::string(layout) : std::format("{}({})", layout, variant);
    const auto* found = std::ranges::find(kLayouts, std::string_view(name), &std::pair<std::string_view, std::wstring_view>::first);
    if (found == kLayouts.end()) {
        found = std::ranges::find(kLayouts, layout, &std::pair<std::string_view, std::wstring_view>::first);
    }
    if (found == kLayouts.end()) {
        return Unexpected(std::format("Unknown keyboard region '{}'", name));
    }

    const int count = GetKeyboardLayoutList(0, nullptr);
    std::vector<HKL> loaded(static_cast<size_t>(std::max(count, 0)));
    GetKeyboardLayoutList(count, loaded.data());

    const std::wstring identifier(found->second);
    HKL handle = LoadKeyboardLayoutW(identifier.c_str(), KLF_NOTELLSHELL);
    if (handle == nullptr) {
        return Unexpected(std::format("Keyboard layout for region '{}' is not available", name));
    }
    const bool owned = std::ranges::find(loaded, handle) == loaded.end();
    return Keymap(std::unique_ptr<void, KeyboardLayoutDeleter>(handle, KeyboardLayoutDeleter{.owned = owned}));
}

auto Keymap::KeySym(VirtualKey key, uint8_t modifiers) const -> uint32_t {
    const auto translated = Translate(static_cast<HKL>(layout_.get()), key, modifiers);
    const uint32_t codepoint = FirstCodepoint(translated.text);
    return translated.dead && codepoint != 0 ? kDeadKeyBit | codepoint : codepoint;
}

auto Keymap::Text(VirtualKey key, uint8_t modifiers) const -> std::string {
    const auto translated = Translate(static_cast<HKL>(layout_.get()), key, modifiers);
    return translated.dead ? std::string() : Narrow(translated.text);
}

auto Keymap::Find(char32_t character) const -> std::optional<TranslatedKey> {
    const auto wanted = static_cast<uint32_t>(character);
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
    return Narrow(Translate(static_cast<HKL>(layout_.get()), key, modifier).text);
}

auto Keymap::HasAltGr() const -> bool {
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
    return std::ranges::any_of(kDigits, [this](VirtualKey key) -> bool {
        const auto altgr = KeySym(key, kModMeta);
        return altgr != 0 && altgr != KeySym(key, 0);
    });
}
