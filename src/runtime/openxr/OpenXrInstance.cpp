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

#include "OpenXrInstance.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <string_view>

#include "Device.hpp"
#include "Logger.hpp"

namespace
{

constexpr uint32_t kOverlaySessionLayersPlacement = 5;

auto FormatVersion(XrVersion version) -> std::string
{
    return std::format("{}.{}.{}", XR_VERSION_MAJOR(version), XR_VERSION_MINOR(version), XR_VERSION_PATCH(version));
}

auto AvailableExtensions() -> Result<std::vector<std::string>>
{
    uint32_t count = 0;
    if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr)))
    {
        return Unexpected("Failed to enumerate OpenXR extensions.");
    }
    std::vector<XrExtensionProperties> properties(count, {.type = XR_TYPE_EXTENSION_PROPERTIES});
    if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, count, &count, properties.data())))
    {
        return Unexpected("Failed to enumerate OpenXR extensions.");
    }
    std::vector<std::string> names;
    names.reserve(count);
    for (const auto& property : properties)
    {
        names.emplace_back(property.extensionName);
    }
    return names;
}

} // namespace

auto OpenXrInstance::Create() -> Result<std::unique_ptr<OpenXrInstance>>
{
    auto instance = std::unique_ptr<OpenXrInstance>(new OpenXrInstance());
    RETURN_IF_ERROR(instance->Initialize());
    return instance;
}

OpenXrInstance::~OpenXrInstance()
{
    if (m_instance != XR_NULL_HANDLE)
    {
        xrDestroyInstance(m_instance);
    }
}

auto OpenXrInstance::Initialize() -> Result<>
{
    ASSIGN_OR_RETURN(const auto available, AvailableExtensions());
    const auto has = [&](std::string_view name) -> bool { return std::ranges::find(available, name) != available.end(); };

    if (!has(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME))
    {
        return Unexpected("Missing KHR_vulkan_enable2 extension.");
    }
    if (!has(XR_EXTX_OVERLAY_EXTENSION_NAME))
    {
        return Unexpected("Missing EXTX_overlay extension.");
    }

    std::vector<const char*> enabled{XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME, XR_EXTX_OVERLAY_EXTENSION_NAME};
    if (has(XR_KHR_BINDING_MODIFICATION_EXTENSION_NAME) && has(XR_EXT_DPAD_BINDING_EXTENSION_NAME))
    {
        enabled.push_back(XR_KHR_BINDING_MODIFICATION_EXTENSION_NAME);
        enabled.push_back(XR_EXT_DPAD_BINDING_EXTENSION_NAME);
    }
    else
    {
        LOG_WARN("Missing EXT_dpad_binding extension.");
    }
    if (has(XR_EXT_HP_MIXED_REALITY_CONTROLLER_EXTENSION_NAME))
    {
        enabled.push_back(XR_EXT_HP_MIXED_REALITY_CONTROLLER_EXTENSION_NAME);
    }
    else
    {
        LOG_WARN("Missing EXT_hp_mixed_reality_controller extension.");
    }
    if (has(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME))
    {
        enabled.push_back(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
    }
    else
    {
        LOG_WARN("Missing EXT_composition_layer_cylinder extension.");
    }
    if (has(XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME))
    {
        enabled.push_back(XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME);
        m_has_equirect2 = true;
    }
    else
    {
        LOG_WARN("Missing EXT_composition_layer_equirect2 extension.");
    }

    XrInstanceCreateInfo create_info{.type = XR_TYPE_INSTANCE_CREATE_INFO};
    std::strncpy(create_info.applicationInfo.applicationName, "framexr", XR_MAX_APPLICATION_NAME_SIZE - 1);
    std::strncpy(create_info.applicationInfo.engineName, "framexr", XR_MAX_ENGINE_NAME_SIZE - 1);
    create_info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    create_info.enabledExtensionCount = static_cast<uint32_t>(enabled.size());
    create_info.enabledExtensionNames = enabled.data();
    if (XR_FAILED(xrCreateInstance(&create_info, &m_instance)))
    {
        m_instance = XR_NULL_HANDLE;
        return Unexpected("Failed to create OpenXR instance.");
    }

    XrInstanceProperties properties{.type = XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_FAILED(xrGetInstanceProperties(m_instance, &properties)))
    {
        return Unexpected("Failed to query OpenXR instance properties.");
    }
    LOG_INFO("Using OpenXR runtime: {} {}", properties.runtimeName, FormatVersion(properties.runtimeVersion));

    const XrSystemGetInfo system_info{.type = XR_TYPE_SYSTEM_GET_INFO, .formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY};
    if (XR_FAILED(xrGetSystem(m_instance, &system_info, &m_system)))
    {
        return Unexpected("Failed to access OpenXR HMD system.");
    }

    RETURN_IF_ERROR(
        LoadFunction("xrGetVulkanGraphicsRequirements2KHR", reinterpret_cast<PFN_xrVoidFunction*>(&m_get_graphics_requirements))
    );
    RETURN_IF_ERROR(LoadFunction("xrCreateVulkanInstanceKHR", reinterpret_cast<PFN_xrVoidFunction*>(&m_create_vulkan_instance)));
    RETURN_IF_ERROR(
        LoadFunction("xrGetVulkanGraphicsDevice2KHR", reinterpret_cast<PFN_xrVoidFunction*>(&m_get_vulkan_graphics_device))
    );
    RETURN_IF_ERROR(LoadFunction("xrCreateVulkanDeviceKHR", reinterpret_cast<PFN_xrVoidFunction*>(&m_create_vulkan_device)));

    XrGraphicsRequirementsVulkan2KHR requirements{.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    if (XR_FAILED(m_get_graphics_requirements(m_instance, m_system, &requirements)))
    {
        return Unexpected("Failed to query OpenXR Vulkan requirements.");
    }

    constexpr XrVersion kTargetVersion = XR_MAKE_VERSION(1, 1, 0);
    if (kTargetVersion < requirements.minApiVersionSupported
        || XR_VERSION_MAJOR(kTargetVersion) > XR_VERSION_MAJOR(requirements.maxApiVersionSupported))
    {
        return Unexpected(
            std::format(
                "OpenXR runtime requires Vulkan version > {}, < {}.0.0",
                FormatVersion(requirements.minApiVersionSupported),
                XR_VERSION_MAJOR(requirements.maxApiVersionSupported) + 1
            )
        );
    }
    return {};
}

auto OpenXrInstance::LoadFunction(const char* name, PFN_xrVoidFunction* function) const -> Result<>
{
    if (XR_FAILED(xrGetInstanceProcAddr(m_instance, name, function)) || *function == nullptr)
    {
        return Unexpected(std::format("Failed to load OpenXR function {}", name));
    }
    return {};
}

auto OpenXrInstance::ResultName(XrResult result) const -> std::string
{
    std::array<char, XR_MAX_RESULT_STRING_SIZE> buffer{};
    if (m_instance == XR_NULL_HANDLE || XR_FAILED(xrResultToString(m_instance, result, buffer.data())))
    {
        return std::format("XrResult({})", static_cast<int>(result));
    }
    std::string_view name(buffer.data());
    if (name.starts_with("XR_"))
    {
        name.remove_prefix(3);
    }
    return std::string(name);
}

auto OpenXrInstance::Check(XrResult result) const -> Result<>
{
    if (XR_FAILED(result))
    {
        return Unexpected(std::format("OpenXR Error: {}", ResultName(result)));
    }
    return {};
}

auto OpenXrInstance::CreateVulkanInstance(const VkInstanceCreateInfo& create_info) const -> Result<VkInstance>
{
    const XrVulkanInstanceCreateInfoKHR xr_create_info{
        .type = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR,
        .systemId = m_system,
        .pfnGetInstanceProcAddr = &vkGetInstanceProcAddr,
        .vulkanCreateInfo = &create_info,
    };
    VkInstance instance = VK_NULL_HANDLE;
    VkResult vulkan_result = VK_SUCCESS;
    if (const auto result = m_create_vulkan_instance(m_instance, &xr_create_info, &instance, &vulkan_result); XR_FAILED(result))
    {
        FatalError(std::format("XR error creating Vulkan instance: {}", ResultName(result)));
    }
    if (vulkan_result != VK_SUCCESS)
    {
        FatalError(std::format("Vulkan error creating Vulkan instance: {}", static_cast<int>(vulkan_result)));
    }
    return instance;
}

auto OpenXrInstance::VulkanGraphicsDevice(VkInstance instance) const -> Result<VkPhysicalDevice>
{
    const XrVulkanGraphicsDeviceGetInfoKHR get_info{
        .type = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR,
        .systemId = m_system,
        .vulkanInstance = instance,
    };
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    RETURN_IF_ERROR(Check(m_get_vulkan_graphics_device(m_instance, &get_info, &physical_device)));
    return physical_device;
}

auto OpenXrInstance::CreateVulkanDevice(VkPhysicalDevice physical_device, const VkDeviceCreateInfo& create_info) const
    -> Result<VkDevice>
{
    const XrVulkanDeviceCreateInfoKHR xr_create_info{
        .type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR,
        .systemId = m_system,
        .pfnGetInstanceProcAddr = &vkGetInstanceProcAddr,
        .vulkanPhysicalDevice = physical_device,
        .vulkanCreateInfo = &create_info,
    };
    VkDevice device = VK_NULL_HANDLE;
    VkResult vulkan_result = VK_SUCCESS;
    if (const auto result = m_create_vulkan_device(m_instance, &xr_create_info, &device, &vulkan_result); XR_FAILED(result))
    {
        FatalError(std::format("XR error creating Vulkan device: {}", ResultName(result)));
    }
    if (vulkan_result != VK_SUCCESS)
    {
        FatalError(std::format("Vulkan error creating Vulkan device: {}", static_cast<int>(vulkan_result)));
    }
    return device;
}

auto OpenXrInstance::CreateOverlaySession() const -> Result<XrSession>
{
    const XrSessionCreateInfoOverlayEXTX overlay{
        .type = XR_TYPE_SESSION_CREATE_INFO_OVERLAY_EXTX,
        .createFlags = 0,
        .sessionLayersPlacement = kOverlaySessionLayersPlacement,
    };
    const XrGraphicsBindingVulkan2KHR binding{
        .type = XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR,
        .next = &overlay,
        .instance = g_device->Instance(),
        .physicalDevice = g_device->PhysicalDevice(),
        .device = g_device->Handle(),
        .queueFamilyIndex = g_device->GraphicsQueue().Family(),
        .queueIndex = 0,
    };
    const XrSessionCreateInfo create_info{
        .type = XR_TYPE_SESSION_CREATE_INFO,
        .next = &binding,
        .createFlags = 0,
        .systemId = m_system,
    };
    XrSession session = XR_NULL_HANDLE;
    RETURN_IF_ERROR(Check(xrCreateSession(m_instance, &create_info, &session)));
    return session;
}

auto OpenXrInstance::EnvironmentBlendModes() const -> Result<std::vector<XrEnvironmentBlendMode>>
{
    uint32_t count = 0;
    RETURN_IF_ERROR(
        Check(xrEnumerateEnvironmentBlendModes(m_instance, m_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr))
    );
    std::vector<XrEnvironmentBlendMode> modes(count);
    RETURN_IF_ERROR(Check(
        xrEnumerateEnvironmentBlendModes(m_instance, m_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count, &count, modes.data())
    ));
    return modes;
}

auto OpenXrInstance::CreateAction(
    XrActionSet action_set,
    std::string_view name,
    std::string_view localized_name,
    XrActionType type
) const -> Result<XrAction>
{
    XrActionCreateInfo create_info{.type = XR_TYPE_ACTION_CREATE_INFO, .actionType = type};
    name.copy(create_info.actionName, XR_MAX_ACTION_NAME_SIZE - 1);
    localized_name.copy(create_info.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    XrAction action = XR_NULL_HANDLE;
    RETURN_IF_ERROR(Check(xrCreateAction(action_set, &create_info, &action)));
    return action;
}
