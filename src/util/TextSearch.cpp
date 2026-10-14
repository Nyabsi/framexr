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

#include "TextSearch.hpp"

#include <algorithm>
#include <cctype>

auto TextSearch::Fold(std::string_view text) -> std::string
{
    std::string folded(text);
    std::ranges::transform(
        folded,
        folded.begin(),
        [](unsigned char c) -> char { return c == '_' ? ' ' : static_cast<char>(std::tolower(c)); }
    );
    return folded;
}

auto TextSearch::Matches(std::string_view text, std::string_view query) -> bool
{
    return query.empty() || Fold(text).contains(Fold(query));
}
