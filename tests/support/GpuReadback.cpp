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

#include "GpuReadback.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

#include "Device.hpp"
#include "Image.hpp"

auto GpuReadback::Read(Image& image) -> std::vector<unsigned char>
{
    const auto extent = image.Extent();
    const VkDeviceSize size = VkDeviceSize{extent.width} * extent.height * 4;
    const VkDevice handle = g_device->Handle();

    const VkBufferCreateInfo buffer_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer buffer = VK_NULL_HANDLE;
    if (vkCreateBuffer(handle, &buffer_info, nullptr, &buffer) != VK_SUCCESS)
    {
        return {};
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(handle, buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(g_device->PhysicalDevice(), &properties);
    uint32_t type_index = 0;
    constexpr VkMemoryPropertyFlags kWanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
    {
        if ((requirements.memoryTypeBits & (1U << i)) != 0 && (properties.memoryTypes[i].propertyFlags & kWanted) == kWanted)
        {
            type_index = i;
            break;
        }
    }
    const VkMemoryAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = type_index,
    };
    VkDeviceMemory memory = VK_NULL_HANDLE;
    vkAllocateMemory(handle, &allocate_info, nullptr, &memory);
    vkBindBufferMemory(handle, buffer, memory, 0);

    auto& queue = g_device->GraphicsQueue();
    const VkCommandPoolCreateInfo pool_info{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = queue.Family()};
    VkCommandPool pool = VK_NULL_HANDLE;
    vkCreateCommandPool(handle, &pool_info, nullptr, &pool);
    const VkCommandBufferAllocateInfo command_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(handle, &command_info, &command_buffer);
    const VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(command_buffer, &begin_info);
    const VkBufferImageCopy region{
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .imageExtent = {extent.width, extent.height, 1},
    };
    vkCmdCopyImageToBuffer(command_buffer, image.Handle(), image.Layout(), buffer, 1, &region);
    vkEndCommandBuffer(command_buffer);
    const std::array command_buffers{command_buffer};
    const bool submitted = queue.Submit(command_buffers, VK_NULL_HANDLE).has_value();
    queue.WaitIdle();

    std::vector<unsigned char> pixels;
    if (submitted)
    {
        pixels.resize(size);
        void* mapped = nullptr;
        vkMapMemory(handle, memory, 0, size, 0, &mapped);
        std::memcpy(pixels.data(), mapped, size);
        vkUnmapMemory(handle, memory);
    }
    vkDestroyCommandPool(handle, pool, nullptr);
    vkDestroyBuffer(handle, buffer, nullptr);
    vkFreeMemory(handle, memory, nullptr);
    return pixels;
}

auto GpuReadback::DistinctColors(std::span<const unsigned char> rgba) -> size_t
{
    std::vector<uint32_t> colors;
    for (size_t i = 0; i + 3 < rgba.size() && colors.size() <= 64; i += size_t{4} * 7)
    {
        uint32_t color = 0;
        std::memcpy(&color, &rgba[i], 4);
        if (std::ranges::find(colors, color) == colors.end())
        {
            colors.push_back(color);
        }
    }
    return colors.size();
}

auto GpuReadback::IsOpaque(std::span<const unsigned char> rgba) -> bool
{
    for (size_t i = 3; i < rgba.size(); i += 4)
    {
        if (rgba[i] != 0xff)
        {
            return false;
        }
    }
    return !rgba.empty();
}
