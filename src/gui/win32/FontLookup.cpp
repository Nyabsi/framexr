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

#include "Font.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string_view>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace {

constexpr auto kFallbacks = std::to_array<std::string_view>({
    "segoeuib.ttf",
    "segoeui.ttf",
    "YuGothB.ttc",
    "YuGothM.ttc",
    "msgothic.ttc",
    "malgunbd.ttf",
    "msyhbd.ttc",
    "seguisym.ttf",
    "seguiemj.ttf",
});

auto FontsDirectory() -> std::filesystem::path {
    const char* windows = std::getenv("WINDIR");
    return std::filesystem::path(windows != nullptr ? windows : "C:\\Windows") / "Fonts";
}

auto Covers(FT_Library library, const std::filesystem::path& path, uint32_t codepoint) -> bool {
    FT_Face face = nullptr;
    if (FT_New_Face(library, path.string().c_str(), 0, &face) != 0) {
        return false;
    }
    const bool covered = FT_Get_Char_Index(face, codepoint) != 0;
    FT_Done_Face(face);
    return covered;
}

} // namespace

auto FontCache::InitLocator() -> void {
}

auto FontCache::Locate(uint32_t codepoint, int) const -> std::optional<FontFile> {
    std::error_code error;
    if (const std::filesystem::path primary(primary_font_);
        std::filesystem::is_regular_file(primary, error) && Covers(library_, primary, codepoint)) {
        return FontFile{.path = primary.string(), .name = primary.filename().string()};
    }
    const auto directory = FontsDirectory();
    for (const auto file : kFallbacks) {
        const auto path = directory / file;
        if (std::filesystem::is_regular_file(path, error) && Covers(library_, path, codepoint)) {
            return FontFile{.path = path.string(), .name = std::string(file)};
        }
    }
    return std::nullopt;
}
