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

#include <cstddef>
#include <cstdint>
#include <span>

#include <vulkan/vulkan.h>

#include "Error.hpp"

class DdsImage final
{
  public:
    static auto Parse(std::span<const std::byte> file) -> Result<DdsImage>;

    [[nodiscard]] auto Format() const -> VkFormat { return m_format; }
    [[nodiscard]] auto Width() const -> uint32_t { return m_width; }
    [[nodiscard]] auto Height() const -> uint32_t { return m_height; }
    [[nodiscard]] auto Data() const -> std::span<const std::byte> { return m_data; }

  private:
    DdsImage() = default;

    VkFormat m_format = VK_FORMAT_UNDEFINED;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    std::span<const std::byte> m_data;
};
