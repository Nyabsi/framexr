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

#include "ByteWriter.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <utility>

auto ByteWriter::Byte(std::byte value) -> void
{
    m_bytes.push_back(value);
}

auto ByteWriter::Float(float value) -> void
{
    const auto raw = std::bit_cast<uint32_t>(value);
    for (uint32_t shift = 0; shift < 32; shift += 8)
    {
        m_bytes.push_back(static_cast<std::byte>((raw >> shift) & 0xffU));
    }
}

auto ByteWriter::String(std::string_view value) -> void
{
    const auto length = static_cast<uint16_t>(std::min<size_t>(value.size(), UINT16_MAX));
    m_bytes.push_back(static_cast<std::byte>(length & 0xffU));
    m_bytes.push_back(static_cast<std::byte>(length >> 8U));
    const auto truncated = value.substr(0, length);
    const auto data = std::as_bytes(std::span(truncated.begin(), truncated.end()));
    m_bytes.insert(m_bytes.end(), data.begin(), data.end());
}

auto ByteWriter::Take() -> std::vector<std::byte>
{
    return std::move(m_bytes);
}
