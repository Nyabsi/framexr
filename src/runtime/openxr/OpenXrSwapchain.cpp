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

#include "OpenXrSwapchain.hpp"

#include <openxr/openxr_platform.h>

#include "Device.hpp"
#include "OpenXrInstance.hpp"

auto OpenXrSwapchain::Create(const OpenXrInstance& instance, XrSession session, VkExtent2D extent, bool immutable)
    -> Result<std::unique_ptr<OpenXrSwapchain>>
{
    const XrSwapchainCreateInfo create_info{
        .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
        .createFlags = immutable ? XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT : XrSwapchainCreateFlags{0},
        .usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT,
        .format = g_device->NativeFormat(),
        .sampleCount = 1,
        .width = extent.width,
        .height = extent.height,
        .faceCount = 1,
        .arraySize = 1,
        .mipCount = 1,
    };
    XrSwapchain handle = XR_NULL_HANDLE;
    RETURN_IF_ERROR(instance.Check(xrCreateSwapchain(session, &create_info, &handle)));
    auto swapchain = std::unique_ptr<OpenXrSwapchain>(new OpenXrSwapchain(instance, handle, extent));

    uint32_t count = 0;
    RETURN_IF_ERROR(instance.Check(xrEnumerateSwapchainImages(handle, 0, &count, nullptr)));
    std::vector<XrSwapchainImageVulkan2KHR> images(count, {.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
    RETURN_IF_ERROR(instance.Check(
        xrEnumerateSwapchainImages(handle, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()))
    ));

    for (const auto& image : images)
    {
        ASSIGN_OR_RETURN(
            auto wrapped,
            g_device->WrapImage(image.image, g_device->NativeFormat(), extent, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        );
        swapchain->m_images.push_back(std::move(wrapped));
    }
    return swapchain;
}

OpenXrSwapchain::OpenXrSwapchain(const OpenXrInstance& instance, XrSwapchain swapchain, VkExtent2D extent)
    : m_instance(&instance),
      m_swapchain(swapchain),
      m_extent(extent)
{
}

OpenXrSwapchain::~OpenXrSwapchain()
{
    m_images.clear();
    xrDestroySwapchain(m_swapchain);
}

auto OpenXrSwapchain::AcquireWaitImage() -> Result<std::shared_ptr<Image>>
{
    const XrSwapchainImageAcquireInfo acquire_info{.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    uint32_t index = 0;
    RETURN_IF_ERROR(m_instance->Check(xrAcquireSwapchainImage(m_swapchain, &acquire_info, &index)));
    const XrSwapchainImageWaitInfo wait_info{.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_INFINITE_DURATION};
    RETURN_IF_ERROR(m_instance->Check(xrWaitSwapchainImage(m_swapchain, &wait_info)));
    m_ever_acquired = true;
    m_acquired = true;
    return m_images.at(index);
}

auto OpenXrSwapchain::EnsureImageReleased() -> Result<>
{
    if (m_acquired)
    {
        const XrSwapchainImageReleaseInfo release_info{.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        RETURN_IF_ERROR(m_instance->Check(xrReleaseSwapchainImage(m_swapchain, &release_info)));
        m_acquired = false;
    }
    return {};
}

auto OpenXrSwapchain::SubImage() const -> XrSwapchainSubImage
{
    return XrSwapchainSubImage{
        .swapchain = m_swapchain,
        .imageRect =
            {.offset = {.x = 0, .y = 0},
             .extent = {.width = static_cast<int32_t>(m_extent.width), .height = static_cast<int32_t>(m_extent.height)}},
        .imageArrayIndex = 0,
    };
}
