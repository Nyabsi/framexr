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

#include <string_view>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "Config.hpp"
#include "ModularUiConfig.hpp"
#include "Yaml.hpp"
#include "framexr_config.h"

namespace {

constexpr char kWatchYaml[] = {
#embed "watch.yaml"
    ,
    '\0'
};
constexpr char kSettingsYaml[] = {
#embed "settings.yaml"
    ,
    '\0'
};
constexpr char kConfigYaml[] = {
#embed "config.yaml"
    ,
    '\0'
};
constexpr char kActionsJson[] = {
#embed "actions.json"
    ,
    '\0'
};
constexpr char kOpenXrActionsJson[] = {
#embed "openxr_actions.json"
    ,
    '\0'
};

auto ExpectModularUi(std::string_view name, std::string_view text) -> void {
    const auto ui = ConfigStore::LoadKnownYaml<ModularUiConfig>(name, text);
    EXPECT_FALSE(ui.Elements().empty()) << name;
    EXPECT_GT(ui.Size()[0], 0U) << name;
    EXPECT_GT(ui.Size()[1], 0U) << name;
    EXPECT_GT(ui.Width(), 0.0f) << name;
}

} // namespace

TEST(BundledResources, WatchParsesIntoModularUi) {
    ExpectModularUi("watch.yaml", kWatchYaml);
}

TEST(BundledResources, SettingsParsesIntoModularUi) {
    ExpectModularUi("settings.yaml", kSettingsYaml);
}

TEST(BundledResources, ExampleConfigIsValidYaml) {
    EXPECT_TRUE(Yaml::Parse(kConfigYaml).has_value());
}

TEST(BundledResources, OpenVrActionManifestIsValidJson) {
    EXPECT_FALSE(nlohmann::json::parse(kActionsJson, nullptr, false).is_discarded());
}

TEST(BundledResources, OpenXrActionsAreValidJsonWithComments) {
    EXPECT_FALSE(nlohmann::json::parse(kOpenXrActionsJson, nullptr, false, true).is_discarded());
}
