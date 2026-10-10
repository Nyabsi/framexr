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

#include "Buffer.hpp"

#include <cstring>
#include <format>

#include "Device.hpp"

auto Buffer::Create(std::shared_ptr<Device> device, VkDeviceSize size, VkBufferUsageFlags usage)
    -> Result<std::shared_ptr<Buffer>> {
    const VkBufferCreateInfo buffer_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    const VmaAllocationCreateInfo allocation_info{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocation_result{};
    if (const auto
            result = vmaCreateBuffer(device->Allocator(), &buffer_info, &allocation_info, &buffer, &allocation, &allocation_result);
        result != VK_SUCCESS) {
        return Unexpected(std::format("Failed to create buffer of {} bytes: {}", size, static_cast<int>(result)));
    }
    return std::shared_ptr<Buffer>(new Buffer(std::move(device), buffer, allocation, allocation_result.pMappedData, size));
}

Buffer::Buffer(std::shared_ptr<Device> device, VkBuffer buffer, VmaAllocation allocation, void* mapped, VkDeviceSize size)
    : device_(std::move(device)),
      buffer_(buffer),
      allocation_(allocation),
      mapped_(mapped),
      size_(size) {
}

Buffer::~Buffer() {
    vmaDestroyBuffer(device_->Allocator(), buffer_, allocation_);
}

auto Buffer::Write(std::span<const std::byte> data) -> void {
    std::memcpy(mapped_, data.data(), std::min<size_t>(data.size(), size_));
    vmaFlushAllocation(device_->Allocator(), allocation_, 0, VK_WHOLE_SIZE);
}
