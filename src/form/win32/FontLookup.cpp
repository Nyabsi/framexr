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

#include <array>
#include <cstdlib>
#include "FontLookup.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <string_view>
#include <system_error>

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

auto FamilyFiles(FT_Library library) -> const std::map<std::string, std::filesystem::path>& {
    static const auto files = [library]() -> std::map<std::string, std::filesystem::path> {
        std::map<std::string, std::filesystem::path> found;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(FontsDirectory(), error)) {
            const auto extension = entry.path().extension().string();
            if (extension != ".ttf" && extension != ".otf" && extension != ".ttc" && extension != ".TTF" && extension != ".OTF"
                && extension != ".TTC") {
                continue;
            }
            FT_Face face = nullptr;
            if (FT_New_Face(library, entry.path().string().c_str(), 0, &face) != 0) {
                continue;
            }
            if (face->family_name != nullptr) {
                const std::string family(face->family_name);
                const bool bold = (face->style_flags & FT_STYLE_FLAG_BOLD) != 0 && (face->style_flags & FT_STYLE_FLAG_ITALIC) == 0;
                if (bold || !found.contains(family)) {
                    found[family] = entry.path();
                }
            }
            FT_Done_Face(face);
        }
        return found;
    }();
    return files;
}

} // namespace

auto FontLookup::Families(FT_LibraryRec_* library) -> std::vector<std::string> {
    std::vector<std::string> families;
    for (const auto& [family, path] : FamilyFiles(library)) {
        families.push_back(family);
    }
    return families;
}

FontLookup::FontLookup(std::string primary_font) : m_primary_font(std::move(primary_font)) {
}

auto FontLookup::Locate(FT_LibraryRec_* library, uint32_t codepoint, int) const -> std::optional<FontFile> {
    std::error_code error;
    if (m_family) {
        const auto& files = FamilyFiles(library);
        if (const auto it = files.find(*m_family); it != files.end() && Covers(library, it->second, codepoint)) {
            return FontFile{.path = it->second.string(), .name = it->first};
        }
    }
    if (const std::filesystem::path primary(m_primary_font);
        std::filesystem::is_regular_file(primary, error) && Covers(library, primary, codepoint)) {
        return FontFile{.path = primary.string(), .name = primary.filename().string()};
    }
    const auto directory = FontsDirectory();
    for (const auto file : kFallbacks) {
        const auto path = directory / file;
        if (std::filesystem::is_regular_file(path, error) && Covers(library, path, codepoint)) {
            return FontFile{.path = path.string(), .name = std::string(file)};
        }
    }
    return std::nullopt;
}
