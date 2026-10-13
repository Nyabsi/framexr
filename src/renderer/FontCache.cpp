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

#include "FontCache.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "Device.hpp"
#include "Logger.hpp"

FontCache* g_fonts = nullptr;

auto FontCache::Create(std::string primary_font) -> Result<std::unique_ptr<FontCache>> {
    FT_Library library = nullptr;
    if (const auto error = FT_Init_FreeType(&library); error != 0) {
        return Unexpected(std::format("Failed to initialize FreeType: {}", error));
    }
    return std::unique_ptr<FontCache>(new FontCache(std::move(primary_font), library));
}

FontCache::FontCache(std::string primary_font, FT_LibraryRec_* library) : m_library(library), m_lookup(std::move(primary_font)) {
}

FontCache::~FontCache() {
    for (auto& [size, collection] : m_collections) {
        for (auto& font : collection.fonts) {
            FT_Done_Face(font.face);
        }
    }
    FT_Done_FreeType(m_library);
}

auto FontCache::SplitLines(std::string_view text) -> std::vector<std::string_view> {
    std::vector<std::string_view> lines;
    while (!text.empty()) {
        const auto newline = text.find('\n');
        auto line = text.substr(0, newline);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        if (newline == std::string_view::npos) {
            break;
        }
        text.remove_prefix(newline + 1);
    }
    return lines;
}

auto FontCache::DecodeUtf8(std::string_view text) -> std::vector<uint32_t> {
    constexpr uint32_t kReplacement = 0xFFFD;
    std::vector<uint32_t> codepoints;
    size_t index = 0;
    while (index < text.size()) {
        const auto lead = static_cast<uint8_t>(text[index]);
        size_t length = 1;
        uint32_t codepoint = lead;
        if (lead >= 0xF0) {
            length = 4;
            codepoint = lead & 0x07U;
        } else if (lead >= 0xE0) {
            length = 3;
            codepoint = lead & 0x0FU;
        } else if (lead >= 0xC0) {
            length = 2;
            codepoint = lead & 0x1FU;
        } else if (lead >= 0x80) {
            codepoints.push_back(kReplacement);
            ++index;
            continue;
        }
        if (index + length > text.size()) {
            codepoints.push_back(kReplacement);
            break;
        }
        for (size_t offset = 1; offset < length; ++offset) {
            codepoint = (codepoint << 6) | (static_cast<uint8_t>(text[index + offset]) & 0x3FU);
        }
        codepoints.push_back(codepoint);
        index += length;
    }
    return codepoints;
}

auto FontCache::EncodeUtf8(std::span<const uint32_t> codepoints) -> std::string {
    std::string text;
    const auto byte = [&text](uint32_t value) -> void { text += static_cast<char>(static_cast<unsigned char>(value)); };
    for (const auto codepoint : codepoints) {
        if (codepoint < 0x80U) {
            byte(codepoint);
        } else if (codepoint < 0x800U) {
            byte(0xC0U | (codepoint >> 6U));
            byte(0x80U | (codepoint & 0x3FU));
        } else if (codepoint < 0x10000U) {
            byte(0xE0U | (codepoint >> 12U));
            byte(0x80U | ((codepoint >> 6U) & 0x3FU));
            byte(0x80U | (codepoint & 0x3FU));
        } else {
            byte(0xF0U | (codepoint >> 18U));
            byte(0x80U | ((codepoint >> 12U) & 0x3FU));
            byte(0x80U | ((codepoint >> 6U) & 0x3FU));
            byte(0x80U | (codepoint & 0x3FU));
        }
    }
    return text;
}

auto FontCache::Width(std::string_view text, int size) -> float {
    if (g_fonts != nullptr && g_device != nullptr) {
        if (const auto measured = g_fonts->MeasureText(text, size)) {
            return measured->width;
        }
    }
    return 0.6f * static_cast<float>(size) * static_cast<float>(text.size());
}

auto FontCache::Ellipsize(std::string_view text, int size, float width) -> std::string {
    if (Width(text, size) <= width) {
        return std::string(text);
    }
    auto codepoints = DecodeUtf8(text);
    while (!codepoints.empty()) {
        codepoints.pop_back();
        auto shortened = EncodeUtf8(codepoints) + "...";
        if (Width(shortened, size) <= width) {
            return shortened;
        }
    }
    return "...";
}

auto FontCache::Collection(int size) -> FontCollection& {
    auto [it, inserted] = m_collections.try_emplace(size);
    if (inserted) {
        it->second.zero_glyph = std::make_shared<Glyph>(Glyph{.advance = static_cast<float>(size) / 3.0f});
    }
    return it->second;
}

auto FontCache::FontForCodepoint(uint32_t codepoint, int size) -> size_t {
    auto& collection = Collection(size);
    if (const auto it = collection.codepoint_fonts.find(codepoint); it != collection.codepoint_fonts.end()) {
        return it->second;
    }

    const auto located = m_lookup.Locate(m_library, codepoint, size);
    if (!located) {
        collection.codepoint_fonts.emplace(codepoint, 0);
        return 0;
    }
    const auto& [path, name, face_index] = *located;

    LOG_DEBUG("Loading font: {} {}pt", name, size);

    FT_Face face = nullptr;
    if (const auto error = FT_New_Face(m_library, path.c_str(), face_index, &face); error != 0) {
        LOG_WARN("Failed to load font at {}: {}", path, error);
        collection.codepoint_fonts.emplace(codepoint, 0);
        return 0;
    }
    const auto char_size = static_cast<FT_F26Dot6>(size) << 6;
    if (const auto error = FT_Set_Char_Size(face, char_size, char_size, 96, 96); error != 0) {
        LOG_WARN("Failed to set font size: {}", error);
        FT_Done_Face(face);
        collection.codepoint_fonts.emplace(codepoint, 0);
        return 0;
    }

    const auto font_index = collection.fonts.size();
    FT_UInt glyph_index = 0;
    for (auto charcode = FT_Get_First_Char(face, &glyph_index); glyph_index != 0;
         charcode = FT_Get_Next_Char(face, charcode, &glyph_index)) {
        collection.codepoint_fonts.emplace(static_cast<uint32_t>(charcode), font_index);
    }
    if (!collection.codepoint_fonts.contains(codepoint)) {
        LOG_WARN("Got font '{}' for CP 0x{:x}, but CP is not present in font!", name, codepoint);
        collection.codepoint_fonts.emplace(codepoint, 0);
    }

    FontFace font{.face = face};
    font.glyphs.emplace(0, std::make_shared<Glyph>(Glyph{.advance = static_cast<float>(size) / 3.0f}));
    collection.fonts.push_back(std::move(font));
    return font_index;
}

auto FontCache::GlyphForCodepoint(uint32_t codepoint, int size, std::optional<CommandBuffer>& uploads)
    -> Result<std::shared_ptr<Glyph>> {
    const auto font_index = FontForCodepoint(codepoint, size);
    auto& collection = Collection(size);
    if (font_index >= collection.fonts.size()) {
        LOG_WARN("No font found for codepoint: 0x{:x}", codepoint);
        return collection.zero_glyph;
    }

    auto& font = collection.fonts[font_index];
    if (const auto it = font.glyphs.find(codepoint); it != font.glyphs.end()) {
        return it->second;
    }

    if (FT_Load_Char(font.face, codepoint, FT_LOAD_DEFAULT) != 0) {
        return collection.zero_glyph;
    }
    const auto* slot = font.face->glyph;
    if (FT_Render_Glyph(font.face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
        return collection.zero_glyph;
    }

    const auto& bitmap = slot->bitmap;
    const auto pitch = static_cast<uint32_t>(std::abs(bitmap.pitch));
    if (bitmap.buffer == nullptr || pitch == 0 || bitmap.rows == 0) {
        return collection.zero_glyph;
    }

    VkFormat format = VK_FORMAT_UNDEFINED;
    switch (bitmap.pixel_mode) {
    case FT_PIXEL_MODE_GRAY: format = VK_FORMAT_R8_UNORM; break;
    case FT_PIXEL_MODE_GRAY2: format = VK_FORMAT_R16_SFLOAT; break;
    case FT_PIXEL_MODE_GRAY4: format = VK_FORMAT_R32_SFLOAT; break;
    default: return collection.zero_glyph;
    }

    if (!uploads) {
        ASSIGN_OR_RETURN(auto command_buffer, g_device->CreateCommandBuffer(g_device->TransferQueue()));
        uploads.emplace(std::move(command_buffer));
    }

    const std::span pixels(reinterpret_cast<const std::byte*>(bitmap.buffer), static_cast<size_t>(pitch) * bitmap.rows);
    const auto row_length = format == VK_FORMAT_R8_UNORM ? pitch : 0U;
    ASSIGN_OR_RETURN(auto texture, uploads->UploadImage(bitmap.width, bitmap.rows, format, pixels, row_length));

    auto glyph = std::make_shared<Glyph>(Glyph{
        .texture = std::move(texture),
        .top = static_cast<float>(slot->metrics.horiBearingY >> 6),
        .left = static_cast<float>(slot->metrics.horiBearingX >> 6),
        .width = static_cast<float>(bitmap.width),
        .height = static_cast<float>(bitmap.rows),
        .advance = static_cast<float>(slot->metrics.horiAdvance >> 6),
    });
    font.glyphs.emplace(codepoint, glyph);
    return glyph;
}

auto FontCache::MeasureText(std::string_view text, int size) -> Result<TextSize> {
    const auto lines = SplitLines(text);
    const auto size_f = static_cast<float>(size);
    TextSize result{
        .width = size_f * 0.33f,
        .height = std::fma(static_cast<float>(lines.size()) - 1.0f, size_f * 1.5f, size_f),
    };

    std::optional<CommandBuffer> uploads;
    for (const auto line : lines) {
        float width = 0.0f;
        for (const auto codepoint : DecodeUtf8(line)) {
            if (const auto glyph = GlyphForCodepoint(codepoint, size, uploads)) {
                width += (*glyph)->advance;
            }
        }
        result.width = std::max(result.width, width);
    }
    if (uploads) {
        RETURN_IF_ERROR(uploads->SubmitUpload());
    }
    return result;
}

auto FontCache::Glyphs(std::string_view text, int size) -> Result<std::vector<std::shared_ptr<Glyph>>> {
    std::vector<std::shared_ptr<Glyph>> glyphs;
    std::optional<CommandBuffer> uploads;
    for (const auto line : SplitLines(text)) {
        for (const auto codepoint : DecodeUtf8(line)) {
            ASSIGN_OR_RETURN(auto glyph, GlyphForCodepoint(codepoint, size, uploads));
            glyphs.push_back(std::move(glyph));
        }
    }
    if (uploads) {
        RETURN_IF_ERROR(uploads->SubmitUpload());
    }
    return glyphs;
}
