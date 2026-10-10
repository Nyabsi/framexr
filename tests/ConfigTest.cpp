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

#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "Config.hpp"

TEST(Config, DefaultsMatchOriginal) {
    const auto config = Config::Load();
    EXPECT_EQ(config.capture_method, "auto");
    EXPECT_EQ(config.keyboard_scale, 1.0f);
    EXPECT_EQ(config.desktop_view_scale, 1.0f);
    EXPECT_EQ(config.osc_out_port, uint16_t{9000});
    EXPECT_EQ(config.click_freeze_time_ms, 300U);
    EXPECT_EQ(config.mouse_move_interval_ms, 10U);
    EXPECT_EQ(config.primary_font, "LiberationSans:style=Bold");
    EXPECT_TRUE(config.watch_pos.isApprox(Eigen::Vector3f(-0.03f, -0.01f, 0.125f)));
    EXPECT_EQ(config.watch_hand, Handedness::Left);
    EXPECT_TRUE(config.keyboard_sound_enabled);
    EXPECT_EQ(config.screen_max_height, uint16_t{1440});
    EXPECT_FALSE(config.screen_render_down);
    EXPECT_EQ(config.timezones.size(), 2U);
}

TEST(Config, MergeReplacesScalarsAndArraysButMergesTables) {
    auto target = nlohmann::json::parse(R"({"a": 1, "list": [1, 2], "table": {"x": 1, "y": 2}})");
    Config::Merge(target, nlohmann::json::parse(R"({"a": 5, "list": [3], "table": {"y": 9, "z": 3}})"));
    EXPECT_EQ(target, nlohmann::json::parse(R"({"a": 5, "list": [3], "table": {"x": 1, "y": 9, "z": 3}})"));
}
