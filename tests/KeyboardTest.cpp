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

#include <array>
#include <format>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Keyboard.hpp"

TEST(KeyboardLayout, BundledLayoutLoadsAndValidates) {
    const auto& layout = KeyboardProfiles::Find("keychron-v1-ansi");
    EXPECT_FALSE(layout.main_layout.empty());
    EXPECT_GT(layout.row_size, 0.0f);
    EXPECT_EQ(layout.main_layout.size(), layout.key_sizes.size());
}

TEST(KeyboardLayout, DefaultIsKeychronV1Ansi) {
    const auto& layout = KeyboardProfiles::Find("keychron-v1-ansi");
    EXPECT_EQ(layout.name, "keychron-v1-ansi");
    EXPECT_FLOAT_EQ(layout.row_size, 16.0f);
    ASSERT_EQ(layout.main_layout.size(), 6U);
    EXPECT_EQ(layout.main_layout[3].front(), "Caps");
    EXPECT_EQ(layout.main_layout[5][3], "Space");
    EXPECT_FLOAT_EQ(layout.key_sizes[5][3], 6.25f);
    EXPECT_EQ(layout.main_layout[4][12], "Up");
}

TEST(KeyboardLayout, LabelsFollowOriginalRules) {
    const auto& layout = KeyboardProfiles::Find("keychron-v1-ansi");
    EXPECT_EQ(layout.LabelForKey("a"), std::vector<std::string>{"a"});
    EXPECT_EQ(layout.LabelForKey("Caps"), std::vector<std::string>{"Caps Lock"});
    EXPECT_EQ(layout.LabelForKey("KP_Multiply"), std::vector<std::string>{"Multiply"});
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

TEST(KeyboardProfiles, BuiltInProfilesKeepTheOriginalLayout) {
    const auto& profiles = KeyboardProfiles::All();
    ASSERT_GE(profiles.size(), 2U);
    EXPECT_EQ(profiles[0].name, "keychron-v1-ansi");
    EXPECT_EQ(profiles[1].name, "full-size");
    EXPECT_FLOAT_EQ(profiles[1].row_size, 23.0f);
    EXPECT_TRUE(profiles[1].macros.contains("KILL"));
    EXPECT_EQ(KeyboardProfiles::After("keychron-v1-ansi").name, "full-size");
    EXPECT_EQ(KeyboardProfiles::After(profiles.back().name).name, "keychron-v1-ansi");
    EXPECT_EQ(KeyboardProfiles::Find("no-such-profile").name, "keychron-v1-ansi");
}

TEST(KeyboardProfiles, UserProfilesReplaceOrExtendBuiltIns) {
    const auto directory = std::filesystem::temp_directory_path()
                         / std::format("framexr-profiles-{}", ::testing::UnitTest::GetInstance()->random_seed());
    std::filesystem::create_directories(directory);
    const auto write = [&directory](std::string_view file, std::string_view name, float row_size) -> void {
        std::ofstream(directory / file) << std::format(
            "name: \"{}\"\nrow_size: {}\nkey_sizes:\n  - [{}]\nmain_layout:\n  - [\"A\"]\nalt_modifier: None\n"
            "exec_commands: {{}}\nmacros: {{}}\nlabels: {{}}\n",
            name,
            row_size,
            row_size
        );
    };
    write("a-override.yaml", "base", 2.0f);
    write("b-mine.yaml", "mine", 3.0f);
    std::ofstream(directory / "c-broken.yaml") << "name: broken\nrow_size: 4\n";
    std::ofstream(directory / "notes.txt") << "ignored";

    constexpr std::string_view
        kBase = "name: base\nrow_size: 1\nkey_sizes:\n  - [1]\nmain_layout:\n  - [\"B\"]\nalt_modifier: None\n"
                "exec_commands: {}\nmacros: {}\nlabels: {}\n";
    const auto built_in = std::to_array<std::string_view>({kBase});
    const auto profiles = KeyboardProfiles::Load(built_in, directory);
    std::filesystem::remove_all(directory);

    ASSERT_EQ(profiles.size(), 2U);
    EXPECT_EQ(profiles[0].name, "base");
    EXPECT_FLOAT_EQ(profiles[0].row_size, 2.0f);
    EXPECT_EQ(profiles[1].name, "mine");
    EXPECT_FLOAT_EQ(profiles[1].row_size, 3.0f);
}

TEST(KeyboardProfiles, InvalidRowsAreRejected) {
    const auto parsed = KeyboardLayout::Parse(
        "name: bad\nrow_size: 2\nkey_sizes:\n  - [1]\nmain_layout:\n  - [\"A\"]\nalt_modifier: None\n"
        "exec_commands: {}\nmacros: {}\nlabels: {}\n"
    );
    EXPECT_FALSE(parsed.has_value());
}
