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

#include "Image.hpp"

#include "Device.hpp"

Image::Image(const ImageResources& resources, const ImageInfo& info)
    : m_resources(resources),
      m_info(info),
      m_layout(info.initial_layout)
{
}

Image::~Image()
{
    const auto device = g_device->Handle();
    if (m_resources.view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device, m_resources.view, nullptr);
    }
    if (m_resources.allocation != VK_NULL_HANDLE)
    {
        vmaDestroyImage(g_device->Allocator(), m_resources.image, m_resources.allocation);
        return;
    }
    if (m_resources.owns_image)
    {
        vkDestroyImage(device, m_resources.image, nullptr);
    }
    if (m_resources.imported_memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, m_resources.imported_memory, nullptr);
    }
}
