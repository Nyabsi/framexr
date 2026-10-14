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

#include "KeyTranslator.hpp"

#include <utility>

namespace
{

constexpr uint8_t kLevelModifiers = kModShift | kModMeta;
constexpr auto kModifierCombinations = std::to_array<uint8_t>({0, kModShift, kModMeta, kModShift | kModMeta});
constexpr uint32_t kFirstKeycode = 8;
constexpr uint32_t kLastKeycode = 255;

} // namespace

auto KeyTranslator::ParseRegion(std::string_view text) -> KeyboardRegion
{
    const auto open = text.find('(');
    if (open == std::string_view::npos || !text.ends_with(')'))
    {
        return {.layout = std::string(text), .variant = {}};
    }
    return {
        .layout = std::string(text.substr(0, open)),
        .variant = std::string(text.substr(open + 1, text.size() - open - 2)),
    };
}

KeyTranslator::KeyTranslator(std::shared_ptr<const Keymap> region, std::shared_ptr<const Keymap> desktop)
    : m_region(std::move(region)),
      m_desktop(std::move(desktop))
{
}

auto KeyTranslator::Create(std::shared_ptr<const Keymap> region, std::shared_ptr<const Keymap> desktop) -> KeyTranslator
{
    KeyTranslator translator(std::move(region), std::move(desktop));
    for (size_t caps = 0; caps < translator.m_desktop_keys.size(); ++caps)
    {
        const uint8_t lock = caps == 1 ? kModCapsLock : 0;
        for (const uint8_t modifiers : kModifierCombinations)
        {
            for (uint32_t code = kFirstKeycode; code <= kLastKeycode; ++code)
            {
                const auto key = static_cast<VirtualKey>(code);
                if (VirtualKeys::ModifierOf(key))
                {
                    continue;
                }
                const uint32_t symbol = translator.m_desktop->KeySym(key, modifiers | lock);
                if (symbol != 0)
                {
                    translator.m_desktop_keys[caps].try_emplace(symbol, TranslatedKey{.key = key, .modifiers = modifiers});
                }
            }
        }
    }
    return translator;
}

auto KeyTranslator::Translate(VirtualKey key, uint8_t modifiers, bool caps_lock) const -> std::optional<TranslatedKey>
{
    const uint8_t query = (modifiers & kLevelModifiers) | (caps_lock ? kModCapsLock : 0);
    const uint32_t wanted = m_region->KeySym(key, query);
    if (wanted == 0 || m_desktop->KeySym(key, query) == wanted)
    {
        return std::nullopt;
    }
    const auto& keys = m_desktop_keys[caps_lock ? 1 : 0];
    if (const auto found = keys.find(wanted); found != keys.end())
    {
        return found->second;
    }
    return std::nullopt;
}
