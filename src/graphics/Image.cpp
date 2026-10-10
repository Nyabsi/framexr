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
    : resources_(resources),
      info_(info),
      layout_(info.initial_layout) {
}

Image::~Image() {
    const auto device = resources_.device->Handle();
    if (resources_.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, resources_.view, nullptr);
    }
    if (resources_.allocation != VK_NULL_HANDLE) {
        vmaDestroyImage(resources_.device->Allocator(), resources_.image, resources_.allocation);
        return;
    }
    if (resources_.owns_image) {
        vkDestroyImage(device, resources_.image, nullptr);
    }
    if (resources_.imported_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, resources_.imported_memory, nullptr);
    }
}
