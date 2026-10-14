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

#include <memory>
#include <vector>

#include <vulkan/vulkan.h>
#include <openxr/openxr.h>

#include "Error.hpp"
#include "Image.hpp"

class OpenXrInstance;

class OpenXrSwapchain final
{
  public:
    static auto Create(const OpenXrInstance& instance, XrSession session, VkExtent2D extent, bool immutable)
        -> Result<std::unique_ptr<OpenXrSwapchain>>;

    ~OpenXrSwapchain();
    OpenXrSwapchain(const OpenXrSwapchain&) = delete;
    auto operator=(const OpenXrSwapchain&) -> OpenXrSwapchain& = delete;

    [[nodiscard]] auto Extent() const -> VkExtent2D { return m_extent; }
    [[nodiscard]] auto EverAcquired() const -> bool { return m_ever_acquired; }

    auto AcquireWaitImage() -> Result<std::shared_ptr<Image>>;
    auto EnsureImageReleased() -> Result<>;
    [[nodiscard]] auto SubImage() const -> XrSwapchainSubImage;

  private:
    OpenXrSwapchain(const OpenXrInstance& instance, XrSwapchain swapchain, VkExtent2D extent);

    const OpenXrInstance* m_instance;
    XrSwapchain m_swapchain;
    VkExtent2D m_extent;
    std::vector<std::shared_ptr<Image>> m_images;
    bool m_acquired = false;
    bool m_ever_acquired = false;
};
