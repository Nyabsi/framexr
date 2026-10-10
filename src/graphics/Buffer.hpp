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

class Device;

class Buffer final {
  public:
    static auto Create(std::shared_ptr<Device> device, VkDeviceSize size, VkBufferUsageFlags usage)
        -> Result<std::shared_ptr<Buffer>>;

    ~Buffer();

    [[nodiscard]] auto Handle() const -> VkBuffer { return buffer_; }
    [[nodiscard]] auto Size() const -> VkDeviceSize { return size_; }

    auto Write(std::span<const std::byte> data) -> void;

    Buffer(const Buffer&) = delete;
    auto operator=(const Buffer&) -> Buffer& = delete;

  private:
    Buffer(std::shared_ptr<Device> device, VkBuffer buffer, VmaAllocation allocation, void* mapped, VkDeviceSize size);

    std::shared_ptr<Device> device_;
    VkBuffer buffer_;
    VmaAllocation allocation_;
    void* mapped_;
    VkDeviceSize size_;
};
