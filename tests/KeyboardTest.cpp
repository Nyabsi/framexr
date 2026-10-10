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

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Keyboard.hpp"

TEST(KeyboardLayout, BundledLayoutLoadsAndValidates) {
    const auto& layout = KeyboardLayout::Instance();
    EXPECT_FALSE(layout.main_layout.empty());
    EXPECT_GT(layout.row_size, 0.0f);
    EXPECT_EQ(layout.main_layout.size(), layout.key_sizes.size());
}

TEST(KeyboardLayout, LabelsFollowOriginalRules) {
    const auto& layout = KeyboardLayout::Instance();
    EXPECT_EQ(layout.LabelForKey("a"), std::vector<std::string>{"a"});
    EXPECT_EQ(layout.LabelForKey("KP_Multiply"), std::vector<std::string>{" *"});
    EXPECT_EQ(layout.LabelForKey("KP_Begin_X"), std::vector<std::string>{"Begin"});
    EXPECT_EQ(layout.LabelForKey("BACKSPACE_X"), std::vector<std::string>{"Backspace"});
    EXPECT_TRUE(layout.LabelForKey("").empty());
}

TEST(KeyboardMacro, VerbsExpandLikeOriginalRegex) {
    const std::vector<std::pair<VirtualKey, bool>> expected{
        {VirtualKey::LCtrl, true},
        {VirtualKey::C, true},
        {VirtualKey::C, false},
        {VirtualKey::LCtrl, false},
    };
    EXPECT_EQ(Keyboard::MacroEvents({"LCtrl DOWN", "C", "LCtrl UP", "bad state"}), expected);
}

TEST(KeyboardMacro, UnknownKeyAbortsWholeMacro) {
    EXPECT_TRUE(Keyboard::MacroEvents({"C", "NoSuchKey"}).empty());
}
