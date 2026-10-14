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

#include <format>

#include "FontLookup.hpp"

#include <algorithm>
#include <format>

#include <fontconfig/fontconfig.h>

FontLookup::FontLookup(std::string primary_font) : m_primary_font(std::move(primary_font))
{
    FcInit();
}

auto FontLookup::Locate(FT_LibraryRec_*, uint32_t codepoint, int size) const -> std::optional<FontFile>
{
    const auto primary = m_family ? std::format("{}:weight=bold", *m_family) : m_primary_font;
    const auto pattern_text = std::format("{}:size={}:charset={:04x}", primary, size, codepoint);
    FcPattern* pattern = FcNameParse(reinterpret_cast<const FcChar8*>(pattern_text.c_str()));
    if (pattern == nullptr)
    {
        return std::nullopt;
    }
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult match_result = FcResultNoMatch;
    FcPattern* match = FcFontMatch(nullptr, pattern, &match_result);
    FcPatternDestroy(pattern);

    FcChar8* file = nullptr;
    if (match == nullptr || FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch)
    {
        if (match != nullptr)
        {
            FcPatternDestroy(match);
        }
        return std::nullopt;
    }

    FontFile located{.path = reinterpret_cast<const char*>(file)};
    FcChar8* full_name = nullptr;
    located.name = FcPatternGetString(match, FC_FULLNAME, 0, &full_name) == FcResultMatch
                     ? std::string(reinterpret_cast<const char*>(full_name))
                     : located.path;
    FcPatternGetInteger(match, FC_INDEX, 0, &located.index);
    FcPatternDestroy(match);
    return located;
}

auto FontLookup::Families(FT_LibraryRec_*) -> std::vector<std::string>
{
    FcInit();
    FcPattern* pattern = FcPatternCreate();
    FcObjectSet* objects = FcObjectSetBuild(FC_FAMILY, nullptr);
    FcFontSet* fonts = FcFontList(nullptr, pattern, objects);
    std::vector<std::string> families;
    if (fonts != nullptr)
    {
        for (int i = 0; i < fonts->nfont; ++i)
        {
            FcChar8* family = nullptr;
            if (FcPatternGetString(fonts->fonts[i], FC_FAMILY, 0, &family) == FcResultMatch)
            {
                families.emplace_back(reinterpret_cast<const char*>(family));
            }
        }
        FcFontSetDestroy(fonts);
    }
    FcObjectSetDestroy(objects);
    FcPatternDestroy(pattern);
    std::ranges::sort(families);
    const auto [first, last] = std::ranges::unique(families);
    families.erase(first, last);
    return families;
}
