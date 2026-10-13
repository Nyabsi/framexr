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

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "KeyboardLayout.hpp"
#include "Yaml.hpp"

namespace {

auto KeyNamed(const KeyboardLayout& layout, VirtualKey wanted) -> const KeyDefinition* {
    for (const auto& key : layout.Keys()) {
        if (const auto* typed = std::get_if<TypeKey>(&key.action); typed != nullptr && typed->key == wanted) {
            return &key;
        }
    }
    return nullptr;
}

auto Emit(const KeyboardLayout& layout) -> std::string {
    return Yaml::Emit(nlohmann::ordered_json(layout));
}

constexpr std::string_view kBase = "name: base\ncolumns: 1\nrows: 1\nkeys:\n  - { at: [0, 0], key: B }\n";

} // namespace

TEST(KeyboardLayout, BundledLayoutLoadsAndValidates) {
    const auto& layout = KeyboardLayout::Find("keychron-v1-ansi");
    EXPECT_FALSE(layout.Keys().empty());
    EXPECT_FLOAT_EQ(layout.Columns(), 16.0f);
    EXPECT_EQ(layout.Rows(), 6);
    EXPECT_TRUE(layout.Validate().has_value());
}

TEST(KeyboardLayout, DefaultIsKeychronV1Ansi) {
    const auto& layout = KeyboardLayout::Find("keychron-v1-ansi");
    EXPECT_EQ(layout.Name(), "keychron-v1-ansi");
    const auto* caps = KeyNamed(layout, VirtualKey::Caps);
    ASSERT_NE(caps, nullptr);
    EXPECT_EQ(caps->row, 3);
    EXPECT_FLOAT_EQ(caps->x, 0.0f);
    const auto* space = KeyNamed(layout, VirtualKey::Space);
    ASSERT_NE(space, nullptr);
    EXPECT_EQ(space->row, 5);
    EXPECT_FLOAT_EQ(space->x, 3.75f);
    EXPECT_FLOAT_EQ(space->width, 6.25f);
    const auto* up = KeyNamed(layout, VirtualKey::Up);
    ASSERT_NE(up, nullptr);
    EXPECT_EQ(up->row, 4);
    EXPECT_FLOAT_EQ(up->x, 14.0f);
    const auto profile = std::ranges::find_if(layout.Keys(), [](const KeyDefinition& key) -> bool {
        return std::holds_alternative<ProfileKey>(key.action);
    });
    ASSERT_NE(profile, layout.Keys().end());
    EXPECT_EQ(layout.Labels(*profile), std::vector<std::string>{"Layout"});
}

TEST(KeyboardLayout, LabelsFollowOriginalRules) {
    EXPECT_EQ(KeyboardLayout::DefaultLabel("a"), std::vector<std::string>{"a"});
    EXPECT_EQ(KeyboardLayout::DefaultLabel("KP_Multiply"), std::vector<std::string>{"Multiply"});
    EXPECT_EQ(KeyboardLayout::DefaultLabel("KP_Begin_X"), std::vector<std::string>{"Begin"});
    EXPECT_EQ(KeyboardLayout::DefaultLabel("BACKSPACE_X"), std::vector<std::string>{"Backspace"});
    EXPECT_TRUE(KeyboardLayout::DefaultLabel("").empty());

    const auto& layout = KeyboardLayout::Find("keychron-v1-ansi");
    const auto* caps = KeyNamed(layout, VirtualKey::Caps);
    const auto* space = KeyNamed(layout, VirtualKey::Space);
    const auto* one = KeyNamed(layout, VirtualKey::N1);
    const auto* f5 = KeyNamed(layout, VirtualKey::F5);
    ASSERT_TRUE(caps != nullptr && space != nullptr && one != nullptr && f5 != nullptr);
    EXPECT_EQ(layout.Labels(*caps), std::vector<std::string>{"Caps Lock"});
    EXPECT_TRUE(layout.Labels(*space).empty());
    EXPECT_FALSE(one->label.has_value());
    EXPECT_EQ(layout.Labels(*one), (std::vector<std::string>{"1", "!"}));
    EXPECT_EQ(layout.Labels(*f5), std::vector<std::string>{"F5"});
}

TEST(KeyboardLayout, ActionKeysRunShortcutsAndCommands) {
    const auto& layout = KeyboardLayout::Find("full-size");
    const auto action_key = [&layout](std::string_view label) -> const ActionKey* {
        for (const auto& key : layout.Keys()) {
            if (key.label && key.label->size() == 1 && key.label->front() == label) {
                return std::get_if<ActionKey>(&key.action);
            }
        }
        return nullptr;
    };
    const auto* kill = action_key("Kill");
    ASSERT_NE(kill, nullptr);
    ASSERT_EQ(kill->press.size(), 1U);
    EXPECT_EQ(
        std::get<ShortcutButtonAction>(kill->press.front()).keys,
        (std::vector<VirtualKey>{VirtualKey::LSuper, VirtualKey::LCtrl, VirtualKey::Escape})
    );
    EXPECT_NE(action_key("Copy"), nullptr);

    const auto parsed = KeyboardLayout::Parse(R"(
name: cmd
columns: 2
rows: 1
keys:
  - at: [0, 0]
    label: [Talk]
    press: [{ type: Exec, command: [stt, --on] }]
    release: [{ type: Exec, command: [stt, --off] }]
  - { at: [1, 0], press: [{ type: Exec, command: [run] }] }
)");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().Message();
    const auto& talk = std::get<ActionKey>(parsed->Keys()[0].action);
    EXPECT_EQ(std::get<ExecButtonAction>(talk.press.front()).command, (std::vector<std::string>{"stt", "--on"}));
    EXPECT_EQ(std::get<ExecButtonAction>(talk.release.front()).command, (std::vector<std::string>{"stt", "--off"}));
    EXPECT_TRUE(std::get<ActionKey>(parsed->Keys()[1].action).release.empty());
    EXPECT_EQ(parsed->Labels(parsed->Keys()[0]), std::vector<std::string>{"Talk"});
}

TEST(KeyboardLayout, SavedProfilesLoadBackTheSameKeyboard) {
    for (const auto& profile : KeyboardLayout::All()) {
        const auto saved = Emit(profile);
        const auto reloaded = KeyboardLayout::Parse(saved);
        ASSERT_TRUE(reloaded.has_value()) << profile.Name() << ": " << reloaded.error().Message();
        EXPECT_EQ(Emit(*reloaded), saved) << profile.Name();
        EXPECT_EQ(reloaded->Keys().size(), profile.Keys().size());
    }
}

TEST(KeyboardLayout, GridQueriesFindKeysAndFreeCells) {
    auto layout = *KeyboardLayout::Parse(R"(
name: grid
columns: 4
rows: 2
keys:
  - { at: [0, 0], key: A, width: 1.5 }
  - { at: [2, 0], action: Profile }
  - { at: [0, 1], width: 4, key: Space, label: [] }
)");
    EXPECT_EQ(layout.KeyAt(1.25f, 0), 0U);
    EXPECT_FALSE(layout.KeyAt(1.5f, 0).has_value());
    EXPECT_FALSE(layout.Fits(1.0f, 0, 1.0f));
    EXPECT_TRUE(layout.Fits(1.5f, 0, 0.5f));
    EXPECT_TRUE(layout.Fits(0.0f, 0, 2.0f, 0U));
    EXPECT_FALSE(layout.Fits(3.5f, 0, 1.0f));
    EXPECT_EQ(layout.FreeCells(), (std::vector<std::pair<float, int>>{{3.0f, 0}}));
    EXPECT_FLOAT_EQ(KeyboardLayout::Snap(1.13f), 1.25f);
}

TEST(KeyboardProfiles, BuiltInProfilesKeepTheOriginalLayout) {
    const auto& profiles = KeyboardLayout::All();
    ASSERT_GE(profiles.size(), 2U);
    EXPECT_EQ(profiles[0].Name(), "keychron-v1-ansi");
    EXPECT_EQ(profiles[1].Name(), "full-size");
    EXPECT_FLOAT_EQ(profiles[1].Columns(), 23.0f);
    EXPECT_TRUE(KeyboardLayout::IsBuiltIn("full-size"));
    EXPECT_FALSE(KeyboardLayout::IsBuiltIn("mine"));
    EXPECT_EQ(KeyboardLayout::After("keychron-v1-ansi").Name(), "full-size");
    EXPECT_EQ(KeyboardLayout::After(profiles.back().Name()).Name(), "keychron-v1-ansi");
    EXPECT_EQ(KeyboardLayout::Find("no-such-profile").Name(), "keychron-v1-ansi");
}

TEST(KeyboardProfiles, UserProfilesReplaceOrExtendBuiltIns) {
    const auto directory = std::filesystem::temp_directory_path()
                         / std::format("framexr-profiles-{}", ::testing::UnitTest::GetInstance()->random_seed());
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "a-override.yaml") << "name: base\ncolumns: 2\nrows: 1\nkeys:\n  - { at: [0, 0], key: A }\n";
    std::ofstream(directory / "b-mine.yaml") << "name: mine\ncolumns: 3\nrows: 1\nkeys:\n  - { at: [0, 0], key: A }\n";
    std::ofstream(directory / "c-broken.yaml") << "name: broken\ncolumns: 4\n";
    std::ofstream(directory / "notes.txt") << "ignored";

    const auto built_in = std::to_array<std::string_view>({kBase});
    const auto profiles = KeyboardLayout::Load(built_in, directory);
    std::filesystem::remove_all(directory);

    ASSERT_EQ(profiles.size(), 2U);
    EXPECT_EQ(profiles[0].Name(), "base");
    EXPECT_FLOAT_EQ(profiles[0].Columns(), 2.0f);
    EXPECT_EQ(profiles[0].Source().filename(), "a-override.yaml");
    EXPECT_EQ(profiles[1].Name(), "mine");
    EXPECT_FLOAT_EQ(profiles[1].Columns(), 3.0f);
}

TEST(KeyboardProfiles, OverlappingOrOutOfGridKeysAreRejected) {
    EXPECT_FALSE(
        KeyboardLayout::Parse(
            "name: bad\ncolumns: 2\nrows: 1\nkeys:\n  - { at: [0, 0], key: A, width: 1.5 }\n"
            "  - { at: [1, 0], key: B }\n"
        )
            .has_value()
    );
    EXPECT_FALSE(KeyboardLayout::Parse("name: bad\ncolumns: 2\nrows: 1\nkeys:\n  - { at: [1.5, 0], key: A }\n").has_value());
    EXPECT_FALSE(KeyboardLayout::Parse("name: bad\ncolumns: 2\nrows: 1\nkeys:\n  - { at: [0, 1], key: A }\n").has_value());
    EXPECT_FALSE(KeyboardLayout::Parse("name: bad\ncolumns: 2\nrows: 1\nkeys:\n  - { at: [0.1, 0], key: A }\n").has_value());
    EXPECT_TRUE(KeyboardLayout::Parse("name: gaps\ncolumns: 4\nrows: 2\nkeys:\n  - { at: [1, 1], key: A }\n").has_value());
}

TEST(KeyboardProfiles, ImeKeyboardsShipWithAndWithoutTheNumpad) {
    const auto has = [](const KeyboardLayout& layout, VirtualKey wanted) -> bool {
        return std::ranges::any_of(layout.Keys(), [wanted](const KeyDefinition& key) -> bool {
            const auto* typed = std::get_if<TypeKey>(&key.action);
            return typed != nullptr && typed->key == wanted;
        });
    };
    const auto ime = std::to_array<std::tuple<std::string_view, std::string_view, VirtualKey>>({
        {"jis", "jp", VirtualKey::Henkan},
        {"korean", "kr", VirtualKey::Hangul},
        {"chinese", "cn", VirtualKey::Space},
    });
    for (const auto& [family, region, special] : ime) {
        for (const auto* variant : {"full-size", "tkl"}) {
            const auto name = std::format("{}-{}", family, variant);
            ASSERT_TRUE(KeyboardLayout::IsBuiltIn(name)) << name;
            const auto& layout = KeyboardLayout::Find(name);
            EXPECT_EQ(layout.Name(), name);
            EXPECT_TRUE(layout.Validate().has_value()) << name;
            EXPECT_EQ(layout.Region(), std::optional<std::string>(region)) << name;
            EXPECT_TRUE(has(layout, special)) << name;
            EXPECT_EQ(has(layout, VirtualKey::Kp5), std::string_view(variant) == "full-size") << name;
            EXPECT_TRUE(std::ranges::any_of(layout.Keys(), [](const KeyDefinition& key) -> bool {
                return std::holds_alternative<ProfileKey>(key.action);
            })) << name;
        }
    }
    EXPECT_TRUE(has(KeyboardLayout::Find("jis-tkl"), VirtualKey::Yen));
    EXPECT_TRUE(has(KeyboardLayout::Find("jis-tkl"), VirtualKey::AbntC1));
    EXPECT_TRUE(has(KeyboardLayout::Find("korean-tkl"), VirtualKey::Hanja));
}
