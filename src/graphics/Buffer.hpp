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

#pragma once

#include <cstddef>
#include <memory>
#include <span>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include "Error.hpp"

class Buffer final {
  public:
    static auto Create(VkDeviceSize size, VkBufferUsageFlags usage) -> Result<std::shared_ptr<Buffer>>;

    ~Buffer();

    [[nodiscard]] auto Handle() const -> VkBuffer { return m_buffer; }
    [[nodiscard]] auto Size() const -> VkDeviceSize { return m_size; }

    auto Write(std::span<const std::byte> data) -> void;

    Buffer(const Buffer&) = delete;
    auto operator=(const Buffer&) -> Buffer& = delete;

  private:
    Buffer(VkBuffer buffer, VmaAllocation allocation, void* mapped, VkDeviceSize size);

    VkBuffer m_buffer;
    VmaAllocation m_allocation;
    void* m_mapped;
    VkDeviceSize m_size;
};
