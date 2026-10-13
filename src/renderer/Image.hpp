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

#include <atomic>
#include <memory>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct ImageResources {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkDeviceMemory imported_memory = VK_NULL_HANDLE;
    bool owns_image = true;
};

struct ImageInfo {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    VkImageUsageFlags usage = 0;
    VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout final_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};

class Image final {
  public:
    Image(const ImageResources& resources, const ImageInfo& info);
    ~Image();

    [[nodiscard]] auto Handle() const -> VkImage { return m_resources.image; }
    [[nodiscard]] auto View() const -> VkImageView { return m_resources.view; }
    [[nodiscard]] auto Format() const -> VkFormat { return m_info.format; }
    [[nodiscard]] auto Extent() const -> VkExtent2D { return m_info.extent; }
    [[nodiscard]] auto Usage() const -> VkImageUsageFlags { return m_info.usage; }
    [[nodiscard]] auto FinalLayout() const -> VkImageLayout { return m_info.final_layout; }
    [[nodiscard]] auto Layout() const -> VkImageLayout { return m_layout.load(); }

    auto SetLayout(VkImageLayout layout) -> void { m_layout.store(layout); }

    Image(const Image&) = delete;
    auto operator=(const Image&) -> Image& = delete;

  private:
    ImageResources m_resources;
    ImageInfo m_info;
    std::atomic<VkImageLayout> m_layout;
};
