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

#include <chrono>
#include <string>

#include <gtest/gtest.h>

#include "ClockComponent.hpp"
#include "FontCache.hpp"

namespace
{

auto Format(std::string_view format) -> std::string
{
    using namespace std::chrono;
    const sys_time<seconds> instant = sys_days{year{2024} / March / 5} + hours{7} + minutes{8} + seconds{9};
    return ClockComponent::FormatTime(format, locate_zone("UTC"), instant);
}

} // namespace

TEST(LabelFormatTime, MatchesChronoStrftime)
{
    EXPECT_EQ(Format("%H:%M"), "07:08");
    EXPECT_EQ(Format("%H:%M:%S"), "07:08:09");
    EXPECT_EQ(Format("%I:%M %p"), "07:08 AM");
    EXPECT_EQ(Format("%a %b %e"), "Tue Mar  5");
    EXPECT_EQ(Format("%Y-%m-%d"), "2024-03-05");
    EXPECT_EQ(Format("%A, %B %d"), "Tuesday, March 05");
    EXPECT_EQ(Format("%x"), "03/05/24");
    EXPECT_EQ(Format("%D"), "03/05/24");
    EXPECT_EQ(Format("%j %u %w"), "065 2 2");
    EXPECT_EQ(Format("%%"), "%");
}

TEST(LabelWrap, BreaksAtSpacesWithinWidth)
{
    EXPECT_EQ(FontCache::Wrap("hello world again", 10, 60.0f), "hello\nworld\nagain");
    EXPECT_EQ(FontCache::Wrap("short", 10, 60.0f), "short");
    EXPECT_EQ(FontCache::Wrap("one two\nthree", 10, 60.0f), "one two\nthree");
}

TEST(LabelWrap, SplitsWordsLongerThanWidth)
{
    EXPECT_EQ(FontCache::Wrap("abcdefghijklmno", 10, 60.0f), "abcdefghij\nklmno");
    EXPECT_EQ(FontCache::Wrap("x", 10, 1.0f), "x");
}
