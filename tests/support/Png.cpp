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

#include "Png.hpp"

#include <algorithm>
#include <fstream>
#include <string_view>
#include <vector>

namespace
{

auto Crc32(std::span<const unsigned char> data) -> uint32_t
{
    uint32_t crc = ~0U;
    for (const unsigned char byte : data)
    {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

auto AppendBigEndian(std::vector<unsigned char>& out, uint32_t value) -> void
{
    for (int shift = 24; shift >= 0; shift -= 8)
    {
        out.push_back(static_cast<unsigned char>(value >> static_cast<uint32_t>(shift)));
    }
}

} // namespace

auto Png::Write(const std::filesystem::path& path, uint32_t width, uint32_t height, std::span<const unsigned char> rgba) -> void
{
    std::vector<unsigned char> raw;
    raw.reserve((size_t{width} * 4 + 1) * height);
    for (uint32_t y = 0; y < height; ++y)
    {
        raw.push_back(0);
        const auto row = rgba.subspan(size_t{y} * width * 4, size_t{width} * 4);
        raw.insert(raw.end(), row.begin(), row.end());
    }

    std::vector<unsigned char> zlib{0x78, 0x01};
    uint32_t adler_a = 1;
    uint32_t adler_b = 0;
    for (const unsigned char byte : raw)
    {
        adler_a = (adler_a + byte) % 65521U;
        adler_b = (adler_b + adler_a) % 65521U;
    }
    for (size_t offset = 0; offset < raw.size(); offset += 65535)
    {
        const auto length = static_cast<uint32_t>(std::min<size_t>(65535, raw.size() - offset));
        zlib.push_back(offset + length >= raw.size() ? 1 : 0);
        zlib.push_back(static_cast<unsigned char>(length & 0xffU));
        zlib.push_back(static_cast<unsigned char>(length >> 8U));
        zlib.push_back(static_cast<unsigned char>(~length & 0xffU));
        zlib.push_back(static_cast<unsigned char>((~length >> 8U) & 0xffU));
        const auto begin = raw.begin() + static_cast<std::ptrdiff_t>(offset);
        zlib.insert(zlib.end(), begin, begin + static_cast<std::ptrdiff_t>(length));
    }
    AppendBigEndian(zlib, (adler_b << 16U) | adler_a);

    std::vector<unsigned char> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    const auto chunk = [&png](std::string_view type, std::span<const unsigned char> data) -> void
    {
        AppendBigEndian(png, static_cast<uint32_t>(data.size()));
        std::vector<unsigned char> body(type.begin(), type.end());
        body.insert(body.end(), data.begin(), data.end());
        png.insert(png.end(), body.begin(), body.end());
        AppendBigEndian(png, Crc32(body));
    };
    std::vector<unsigned char> header;
    AppendBigEndian(header, width);
    AppendBigEndian(header, height);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    chunk("IHDR", header);
    chunk("IDAT", zlib);
    chunk("IEND", {});

    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
}
