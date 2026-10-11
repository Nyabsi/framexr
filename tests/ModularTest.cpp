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

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "Theme.hpp"

TEST(Theme, RoundingScalesTheDefaultRadiusAndCustomRadiusWins) {
    EXPECT_FLOAT_EQ(Theme::Radius(Rounding::Square, Shape::Key), 0.0f);
    EXPECT_FLOAT_EQ(Theme::Radius(Rounding::Subtle, Shape::Control), Theme::Radius(Rounding::Round, Shape::Control) / 2.0f);
    EXPECT_GT(Theme::Radius(Rounding::Round, Shape::Panel), Theme::Radius(Rounding::Round, Shape::Control));
    EXPECT_FLOAT_EQ(Theme::Resolve(5.0f, Rounding::Square, Shape::Panel), 5.0f);
    EXPECT_FLOAT_EQ(Theme::Resolve(std::nullopt, Rounding::Round, Shape::Control), 8.0f);
    EXPECT_EQ(nlohmann::json(Rounding::Subtle).get<Rounding>(), Rounding::Subtle);
}
