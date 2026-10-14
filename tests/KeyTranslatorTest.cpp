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

#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include "Keymap.hpp"
#include "KeyTranslator.hpp"

namespace
{

auto Load(const char* layout) -> std::shared_ptr<const Keymap>
{
    auto loaded = Keymap::FromNames(layout, "");
    if (!loaded)
    {
        return nullptr;
    }
    return std::make_shared<const Keymap>(std::move(*loaded));
}

} // namespace

TEST(KeyboardRegion, ParsesLayoutAndVariant)
{
    const auto plain = KeyTranslator::ParseRegion("fi");
    EXPECT_EQ(plain.layout, "fi");
    EXPECT_TRUE(plain.variant.empty());
    const auto variant = KeyTranslator::ParseRegion("fi(classic)");
    EXPECT_EQ(variant.layout, "fi");
    EXPECT_EQ(variant.variant, "classic");
}

TEST(KeyTranslator, FinnishSymbolsAreTypedWithTheDesktopKeys)
{
    const auto finnish = Load("fi");
    const auto us = Load("us");
    if (!finnish || !us)
    {
        GTEST_SKIP() << "xkb layout data not installed";
    }
    const auto translator = KeyTranslator::Create(finnish, us);

    const uint32_t quote = finnish->KeySym(VirtualKey::N2, kModShift);
    const auto shifted_two = translator.Translate(VirtualKey::N2, kModShift, false);
    ASSERT_TRUE(shifted_two.has_value());
    EXPECT_EQ(us->KeySym(shifted_two->key, shifted_two->modifiers), quote);
    EXPECT_NE(shifted_two->key, VirtualKey::N2);

    const uint32_t plus = finnish->KeySym(VirtualKey::Minus, 0);
    const auto minus = translator.Translate(VirtualKey::Minus, 0, false);
    ASSERT_TRUE(minus.has_value());
    EXPECT_EQ(us->KeySym(minus->key, minus->modifiers), plus);
}

TEST(KeyTranslator, KeysThatAlreadyMatchOrCannotBeTypedAreLeftAlone)
{
    const auto finnish = Load("fi");
    const auto us = Load("us");
    if (!finnish || !us)
    {
        GTEST_SKIP() << "xkb layout data not installed";
    }
    const auto translator = KeyTranslator::Create(finnish, us);
    EXPECT_FALSE(translator.Translate(VirtualKey::A, 0, false).has_value());
    EXPECT_FALSE(translator.Translate(VirtualKey::A, kModShift, true).has_value());
    EXPECT_FALSE(translator.Translate(VirtualKey::Oem1, 0, false).has_value());

    const auto same = KeyTranslator::Create(finnish, finnish);
    EXPECT_FALSE(same.Translate(VirtualKey::N2, kModShift, false).has_value());
    EXPECT_FALSE(same.Translate(VirtualKey::Minus, 0, false).has_value());
}

TEST(KeyTranslator, CapsLockPicksTheCaseOnTheDesktop)
{
    const auto finnish = Load("fi");
    const auto german = Load("de");
    if (!finnish || !german)
    {
        GTEST_SKIP() << "xkb layout data not installed";
    }
    const auto translator = KeyTranslator::Create(finnish, german);
    const uint32_t wanted = finnish->KeySym(VirtualKey::Oem7, kModCapsLock);
    const auto upper = translator.Translate(VirtualKey::Oem7, 0, true);
    if (german->KeySym(VirtualKey::Oem7, kModCapsLock) == wanted)
    {
        EXPECT_FALSE(upper.has_value());
    }
    else
    {
        ASSERT_TRUE(upper.has_value());
        EXPECT_EQ(german->KeySym(upper->key, static_cast<uint8_t>(upper->modifiers | kModCapsLock)), wanted);
    }
}

TEST(KeyTranslator, JisSymbolsAreTypedWithTheDesktopKeys)
{
    const auto japanese = Load("jp");
    const auto us = Load("us");
    if (!japanese || !us)
    {
        GTEST_SKIP() << "xkb layout data not installed";
    }
    const auto translator = KeyTranslator::Create(japanese, us);
    for (const auto key : {VirtualKey::Oem4, VirtualKey::Oem7, VirtualKey::Yen, VirtualKey::AbntC1})
    {
        const uint32_t wanted = japanese->KeySym(key, 0);
        const auto typed = translator.Translate(key, 0, false);
        if (us->KeySym(key, 0) == wanted)
        {
            EXPECT_FALSE(typed.has_value());
            continue;
        }
        ASSERT_TRUE(typed.has_value()) << VirtualKeys::Name(key);
        EXPECT_EQ(us->KeySym(typed->key, typed->modifiers), wanted) << VirtualKeys::Name(key);
    }
}
