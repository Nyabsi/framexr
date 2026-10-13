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

#include <initializer_list>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "ActionRunner.hpp"
#include "document/Actions.hpp"
#include "Keymap.hpp"
#include "Container.hpp"
#include "RecordingHid.hpp"
#include "ScopedRecordingHid.hpp"

namespace {

auto Tap(VirtualKey key, uint8_t modifiers = 0) -> std::vector<HidEvent> {
    return {{.key = key, .down = true, .modifiers = modifiers}, {.key = key, .down = false, .modifiers = modifiers}};
}

auto Join(std::initializer_list<std::vector<HidEvent>> parts) -> std::vector<HidEvent> {
    std::vector<HidEvent> joined;
    for (const auto& part : parts) {
        joined.insert(joined.end(), part.begin(), part.end());
    }
    return joined;
}

} // namespace

TEST(Actions, ShortcutHoldsKeysInOrderAndReleasesInReverse) {
    ScopedRecordingHid hid;
    ActionRunner runner;
    runner.Run(ShortcutButtonAction{.keys = {VirtualKey::LSuper, VirtualKey::LCtrl, VirtualKey::Escape}});
    const std::vector<HidEvent> expected{
        {.key = VirtualKey::LSuper, .down = true},
        {.key = VirtualKey::LCtrl, .down = true},
        {.key = VirtualKey::Escape, .down = true},
        {.key = VirtualKey::Escape, .down = false},
        {.key = VirtualKey::LCtrl, .down = false},
        {.key = VirtualKey::LSuper, .down = false},
    };
    EXPECT_EQ(hid.Events(), expected);
}

TEST(Actions, TypeTextPressesWhateverKeysTheDesktopLayoutNeeds) {
    auto previous = g_interaction->TypingKeymap();
    auto us = Keymap::FromNames("us", "");
    ASSERT_TRUE(us.has_value());
    g_interaction->SetTypingKeymap(std::make_shared<const Keymap>(std::move(*us)));

    ScopedRecordingHid hid;
    ActionRunner runner;
    runner.Run(TypeTextButtonAction{.text = "Hi!\n"});
    EXPECT_EQ(
        hid.Events(),
        Join({Tap(VirtualKey::H, kModShift), Tap(VirtualKey::I), Tap(VirtualKey::N1, kModShift), Tap(VirtualKey::Return)})
    );

    auto finnish = Keymap::FromNames("fi", "");
    ASSERT_TRUE(finnish.has_value());
    g_interaction->SetTypingKeymap(std::make_shared<const Keymap>(std::move(*finnish)));
    hid.Clear();
    runner.Run(TypeTextButtonAction{.text = "ä@"});
    EXPECT_EQ(hid.Events(), Join({Tap(VirtualKey::Oem7), Tap(VirtualKey::N2, kModMeta)}));
    g_interaction->SetTypingKeymap(previous);
}
