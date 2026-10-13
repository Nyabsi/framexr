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
#include <string>
#include <string_view>
#include <vector>

#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "Error.hpp"

class OpenXrInstance final {
  public:
    static auto Create() -> Result<std::unique_ptr<OpenXrInstance>>;

    ~OpenXrInstance();
    OpenXrInstance(const OpenXrInstance&) = delete;
    auto operator=(const OpenXrInstance&) -> OpenXrInstance& = delete;

    [[nodiscard]] auto Handle() const -> XrInstance { return m_instance; }
    [[nodiscard]] auto System() const -> XrSystemId { return m_system; }
    [[nodiscard]] auto HasEquirect2() const -> bool { return m_has_equirect2; }

    [[nodiscard]] auto ResultName(XrResult result) const -> std::string;
    [[nodiscard]] auto Check(XrResult result) const -> Result<>;

    [[nodiscard]] auto CreateVulkanInstance(const VkInstanceCreateInfo& create_info) const -> Result<VkInstance>;
    [[nodiscard]] auto VulkanGraphicsDevice(VkInstance instance) const -> Result<VkPhysicalDevice>;
    [[nodiscard]] auto CreateVulkanDevice(VkPhysicalDevice physical_device, const VkDeviceCreateInfo& create_info) const
        -> Result<VkDevice>;
    [[nodiscard]] auto CreateOverlaySession() const -> Result<XrSession>;
    [[nodiscard]] auto CreateAction(
        XrActionSet action_set,
        std::string_view name,
        std::string_view localized_name,
        XrActionType type
    ) const -> Result<XrAction>;
    [[nodiscard]] auto EnvironmentBlendModes() const -> Result<std::vector<XrEnvironmentBlendMode>>;

  private:
    OpenXrInstance() = default;

    auto Initialize() -> Result<>;
    auto LoadFunction(const char* name, PFN_xrVoidFunction* function) const -> Result<>;

    XrInstance m_instance = XR_NULL_HANDLE;
    XrSystemId m_system = XR_NULL_SYSTEM_ID;
    bool m_has_equirect2 = false;
    PFN_xrGetVulkanGraphicsRequirements2KHR m_get_graphics_requirements = nullptr;
    PFN_xrCreateVulkanInstanceKHR m_create_vulkan_instance = nullptr;
    PFN_xrGetVulkanGraphicsDevice2KHR m_get_vulkan_graphics_device = nullptr;
    PFN_xrCreateVulkanDeviceKHR m_create_vulkan_device = nullptr;
};
