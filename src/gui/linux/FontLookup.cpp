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

#include <fontconfig/fontconfig.h>

#include "Font.hpp"

auto FontCache::InitLocator() -> void {
    FcInit();
}

auto FontCache::Locate(uint32_t codepoint, int size) const -> std::optional<FontFile> {
    const auto pattern_text = std::format("{}:size={}:charset={:04x}", m_primary_font, size, codepoint);
    FcPattern* pattern = FcNameParse(reinterpret_cast<const FcChar8*>(pattern_text.c_str()));
    if (pattern == nullptr) {
        return std::nullopt;
    }
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult match_result = FcResultNoMatch;
    FcPattern* match = FcFontMatch(nullptr, pattern, &match_result);
    FcPatternDestroy(pattern);

    FcChar8* file = nullptr;
    if (match == nullptr || FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch) {
        if (match != nullptr) {
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
