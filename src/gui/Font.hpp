// FrameXR
//
// This is a port of wlx-overlay-s from Rust to C++.
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
// Copyright (C) 2023-2025 galister and wlx-overlay-s contributors
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

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "Error.hpp"
#include "Image.hpp"

struct FT_LibraryRec_;
struct FT_FaceRec_;

struct Glyph {
    std::shared_ptr<Image> texture;
    float top = 0.0f;
    float left = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float advance = 0.0f;
};

struct FontFace {
    FT_FaceRec_* face = nullptr;
    std::unordered_map<uint32_t, std::shared_ptr<Glyph>> glyphs;
};

struct FontCollection {
    std::vector<FontFace> fonts;
    std::unordered_map<uint32_t, size_t> codepoint_fonts;
    std::shared_ptr<Glyph> zero_glyph;
};

struct FontFile {
    std::string path;
    std::string name;
    int index = 0;
};

struct TextSize {
    float width = 0.0f;
    float height = 0.0f;
};

class FontCache final {
  public:
    static auto Create(std::string primary_font) -> Result<std::unique_ptr<FontCache>>;
    static auto SplitLines(std::string_view text) -> std::vector<std::string_view>;
    static auto DecodeUtf8(std::string_view text) -> std::vector<uint32_t>;

    ~FontCache();
    FontCache(const FontCache&) = delete;
    auto operator=(const FontCache&) -> FontCache& = delete;

    auto MeasureText(std::string_view text, int size) -> Result<TextSize>;
    auto Glyphs(std::string_view text, int size) -> Result<std::vector<std::shared_ptr<Glyph>>>;

  private:
    FontCache(std::string primary_font, FT_LibraryRec_* library);

    auto Collection(int size) -> FontCollection&;
    auto FontForCodepoint(uint32_t codepoint, int size) -> size_t;
    [[nodiscard]] auto Locate(uint32_t codepoint, int size) const -> std::optional<FontFile>;
    static auto InitLocator() -> void;
    auto GlyphForCodepoint(uint32_t codepoint, int size, std::optional<CommandBuffer>& uploads) -> Result<std::shared_ptr<Glyph>>;

    std::string m_primary_font;
    FT_LibraryRec_* m_library;
    std::map<int, FontCollection> m_collections;
};

extern FontCache* g_fonts;
