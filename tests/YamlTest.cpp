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

#include <gtest/gtest.h>

#include "Yaml.hpp"

TEST(Yaml, ResolvesScalarsLikeSerdeYaml)
{
    const auto parsed = Yaml::Parse("a: 0x10\nb: 1.5\nc: ~\nd: yes_string\ne: true\nf: !Variant 3\ng: '12'\nh: -7\n");
    ASSERT_TRUE(parsed.has_value());
    const auto& value = *parsed;
    EXPECT_EQ(value["a"].get<int>(), 16);
    EXPECT_DOUBLE_EQ(value["b"].get<double>(), 1.5);
    EXPECT_TRUE(value["c"].is_null());
    EXPECT_EQ(value["d"].get<std::string>(), "yes_string");
    EXPECT_TRUE(value["e"].get<bool>());
    EXPECT_EQ(value["f"]["Variant"].get<int>(), 3);
    EXPECT_EQ(value["g"].get<std::string>(), "12");
    EXPECT_EQ(value["h"].get<int>(), -7);
}

TEST(Yaml, RejectsMalformedDocuments)
{
    EXPECT_FALSE(Yaml::Parse("a: [1, 2").has_value());
}
