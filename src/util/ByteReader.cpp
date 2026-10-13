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

#include "ByteReader.hpp"

#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>

ByteReader::ByteReader(std::span<const std::byte> bytes) : m_bytes(bytes) {
}

auto ByteReader::Byte() -> std::optional<std::byte> {
    if (m_bytes.empty()) {
        return std::nullopt;
    }
    const auto value = m_bytes.front();
    m_bytes = m_bytes.subspan(1);
    return value;
}

auto ByteReader::Float() -> std::optional<float> {
    if (m_bytes.size() < 4) {
        return std::nullopt;
    }
    uint32_t raw = 0;
    for (size_t i = 0; i < 4; ++i) {
        raw |= static_cast<uint32_t>(m_bytes[i]) << (i * 8);
    }
    m_bytes = m_bytes.subspan(4);
    return std::bit_cast<float>(raw);
}

auto ByteReader::String() -> std::optional<std::string> {
    if (m_bytes.size() < 2) {
        return std::nullopt;
    }
    const size_t length = static_cast<size_t>(m_bytes[0]) | (static_cast<size_t>(m_bytes[1]) << 8U);
    if (m_bytes.size() < 2 + length) {
        return std::nullopt;
    }
    std::string value(length, '\0');
    std::memcpy(value.data(), m_bytes.subspan(2).data(), length);
    m_bytes = m_bytes.subspan(2 + length);
    return value;
}

auto ByteReader::Empty() const -> bool {
    return m_bytes.empty();
}
