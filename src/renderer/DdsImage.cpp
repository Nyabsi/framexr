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

#include "DdsImage.hpp"

#include <cstring>
#include <format>

namespace {

constexpr auto MakeFourcc(char a, char b, char c, char d) -> uint32_t {
    return static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) | (static_cast<uint32_t>(c) << 16)
         | (static_cast<uint32_t>(d) << 24);
}

constexpr uint32_t kMagic = MakeFourcc('D', 'D', 'S', ' ');
constexpr size_t kHeaderSize = 128;
constexpr size_t kDx10HeaderSize = 20;
constexpr uint32_t kPixelFormatFourcc = 0x4;
constexpr uint32_t kPixelFormatRgb = 0x40;
constexpr uint32_t kPixelFormatLuminance = 0x20000;

auto ReadU32(std::span<const std::byte> bytes, size_t offset) -> uint32_t {
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

auto FormatFromDxgi(uint32_t dxgi_format) -> VkFormat {
    switch (dxgi_format) {
    case 2: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case 10: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case 28: return VK_FORMAT_R8G8B8A8_UNORM;
    case 29: return VK_FORMAT_R8G8B8A8_SRGB;
    case 61: return VK_FORMAT_R8_UNORM;
    case 71: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case 72: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    case 74: return VK_FORMAT_BC2_UNORM_BLOCK;
    case 75: return VK_FORMAT_BC2_SRGB_BLOCK;
    case 77: return VK_FORMAT_BC3_UNORM_BLOCK;
    case 78: return VK_FORMAT_BC3_SRGB_BLOCK;
    case 80: return VK_FORMAT_BC4_UNORM_BLOCK;
    case 81: return VK_FORMAT_BC4_SNORM_BLOCK;
    case 83: return VK_FORMAT_BC5_UNORM_BLOCK;
    case 84: return VK_FORMAT_BC5_SNORM_BLOCK;
    case 87: return VK_FORMAT_B8G8R8A8_UNORM;
    case 91: return VK_FORMAT_B8G8R8A8_SRGB;
    case 95: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
    case 96: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
    case 98: return VK_FORMAT_BC7_UNORM_BLOCK;
    case 99: return VK_FORMAT_BC7_SRGB_BLOCK;
    default: return VK_FORMAT_UNDEFINED;
    }
}

auto FormatFromFourcc(uint32_t fourcc) -> VkFormat {
    switch (fourcc) {
    case MakeFourcc('D', 'X', 'T', '1'): return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case MakeFourcc('D', 'X', 'T', '2'):
    case MakeFourcc('D', 'X', 'T', '3'): return VK_FORMAT_BC2_UNORM_BLOCK;
    case MakeFourcc('D', 'X', 'T', '4'):
    case MakeFourcc('D', 'X', 'T', '5'): return VK_FORMAT_BC3_UNORM_BLOCK;
    case MakeFourcc('A', 'T', 'I', '1'):
    case MakeFourcc('B', 'C', '4', 'U'): return VK_FORMAT_BC4_UNORM_BLOCK;
    case MakeFourcc('B', 'C', '4', 'S'): return VK_FORMAT_BC4_SNORM_BLOCK;
    case MakeFourcc('A', 'T', 'I', '2'):
    case MakeFourcc('B', 'C', '5', 'U'): return VK_FORMAT_BC5_UNORM_BLOCK;
    case MakeFourcc('B', 'C', '5', 'S'): return VK_FORMAT_BC5_SNORM_BLOCK;
    case 113: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case 116: return VK_FORMAT_R32G32B32A32_SFLOAT;
    default: return VK_FORMAT_UNDEFINED;
    }
}

auto FormatFromMasks(uint32_t flags, uint32_t bit_count, uint32_t red_mask, uint32_t blue_mask) -> VkFormat {
    if ((flags & kPixelFormatLuminance) != 0 && bit_count == 8) {
        return VK_FORMAT_R8_UNORM;
    }
    if ((flags & kPixelFormatRgb) != 0 && bit_count == 32) {
        if (red_mask == 0x000000FF && blue_mask == 0x00FF0000) {
            return VK_FORMAT_R8G8B8A8_UNORM;
        }
        if (red_mask == 0x00FF0000 && blue_mask == 0x000000FF) {
            return VK_FORMAT_B8G8R8A8_UNORM;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

} // namespace

auto DdsImage::Parse(std::span<const std::byte> file) -> Result<DdsImage> {
    if (file.size() < kHeaderSize || ReadU32(file, 0) != kMagic) {
        return Unexpected("Not a valid DDS file.\nSee: https://github.com/galister/wlx-overlay-s/wiki/Custom-Textures");
    }

    DdsImage image;
    image.m_height = ReadU32(file, 12);
    image.m_width = ReadU32(file, 16);
    const auto depth = ReadU32(file, 24);
    const auto pixel_flags = ReadU32(file, 80);
    const auto fourcc = ReadU32(file, 84);

    if (depth > 1) {
        return Unexpected("Not a 2D texture.");
    }

    size_t data_offset = kHeaderSize;
    if ((pixel_flags & kPixelFormatFourcc) != 0 && fourcc == MakeFourcc('D', 'X', '1', '0')) {
        if (file.size() < kHeaderSize + kDx10HeaderSize) {
            return Unexpected("Truncated DDS DX10 header.");
        }
        image.m_format = FormatFromDxgi(ReadU32(file, kHeaderSize));
        data_offset += kDx10HeaderSize;
    } else if ((pixel_flags & kPixelFormatFourcc) != 0) {
        image.m_format = FormatFromFourcc(fourcc);
    } else {
        image.m_format = FormatFromMasks(pixel_flags, ReadU32(file, 88), ReadU32(file, 92), ReadU32(file, 100));
    }

    if (image.m_format == VK_FORMAT_UNDEFINED) {
        return Unexpected(std::format("Unsupported DDS format (fourcc {:#x})", fourcc));
    }

    image.m_data = file.subspan(data_offset);
    return image;
}
