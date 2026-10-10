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

#include "Device.hpp"

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <vk_mem_alloc.h>

#include <unistd.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <format>
#include <ranges>
#include <set>

#include "Logger.hpp"

namespace {

constexpr std::array kDmabufExtensions{
    VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
    VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
    VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
};

constexpr uint32_t kDescriptorPoolSize = 1024;

auto HasExtension(const std::vector<std::string>& extensions, std::string_view name) -> bool {
    return std::ranges::find(extensions, name) != extensions.end();
}

auto DeviceTypePriority(VkPhysicalDevice physical_device) -> uint32_t {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device, &properties);
    switch (properties.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 0;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 1;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 2;
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return 3;
    default: return 4;
    }
}

auto DeviceName(VkPhysicalDevice physical_device) -> std::string {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device, &properties);
    return properties.deviceName;
}

auto TryQueueFamilies(VkPhysicalDevice physical_device, std::vector<VkQueueFlags> requested)
    -> std::optional<std::vector<QueueFamilyRequest>> {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, families.data());

    std::vector<QueueFamilyRequest> result;
    for (uint32_t index = 0; index < count; ++index) {
        auto remaining = families[index].queueCount;
        uint32_t wanted = 0;
        std::erase_if(requested, [&](VkQueueFlags flags) -> bool {
            if ((families[index].queueFlags & flags) != 0 && remaining > 0) {
                --remaining;
                ++wanted;
                return true;
            }
            return false;
        });
        if (wanted > 0) {
            result.push_back({.family = index, .count = wanted});
        }
    }

    if (!requested.empty()) {
        return std::nullopt;
    }
    return result;
}

auto JoinNames(const std::vector<std::string>& names) -> std::string {
    std::string joined;
    for (const auto& name : names) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += name;
    }
    return joined;
}

} // namespace

auto Device::Create(const DeviceCreateInfo& create_info) -> Result<std::shared_ptr<Device>> {
    auto device = std::shared_ptr<Device>(new Device());
    RETURN_IF_ERROR(device->Initialize(create_info));
    return device;
}

auto Device::FourccToFormat(uint32_t fourcc) -> Result<VkFormat> {
    switch (fourcc) {
    case kDrmFormatAbgr8888:
    case kDrmFormatXbgr8888: return VK_FORMAT_R8G8B8A8_UNORM;
    case kDrmFormatArgb8888:
    case kDrmFormatXrgb8888: return VK_FORMAT_B8G8R8A8_UNORM;
    case kDrmFormatAbgr2101010:
    case kDrmFormatXbgr2101010: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    default: return Unexpected(std::format("Unsupported format {}", FourccToString(fourcc)));
    }
}

auto Device::QueueFamilyLayout(VkPhysicalDevice physical_device) -> std::optional<std::vector<QueueFamilyRequest>> {
    if (auto layout = TryQueueFamilies(physical_device, {VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT, VK_QUEUE_TRANSFER_BIT})) {
        return layout;
    }
    if (auto layout = TryQueueFamilies(physical_device, {VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_TRANSFER_BIT, VK_QUEUE_TRANSFER_BIT})) {
        return layout;
    }
    return TryQueueFamilies(physical_device, {VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT});
}

auto Device::SupportedDeviceExtensions(VkPhysicalDevice physical_device) -> std::vector<std::string> {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> properties(count);
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, properties.data());
    std::vector<std::string> names;
    names.reserve(count);
    for (const auto& property : properties) {
        names.emplace_back(property.extensionName);
    }
    return names;
}

auto Device::Initialize(const DeviceCreateInfo& create_info) -> Result<> {
    RETURN_IF_ERROR(CreateInstance(create_info));
    RETURN_IF_ERROR(SelectPhysicalDevice(create_info));
    RETURN_IF_ERROR(CreateLogicalDevice(create_info));
    RETURN_IF_ERROR(CreateAllocator());
    RETURN_IF_ERROR(CreateQuad());
    QueryDrmFormats();
    return {};
}

auto Device::CreateInstance(const DeviceCreateInfo& create_info) -> Result<> {
    std::vector<std::string> extensions = create_info.instance_extensions;
    if (!HasExtension(extensions, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
        extensions.emplace_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    }

    VkInstanceCreateFlags flags = 0;
    if (!create_info.create_instance) {
        uint32_t count = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> available(count);
        vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());
        const bool has_portability = std::ranges::any_of(available, [](const VkExtensionProperties& property) -> bool {
            return std::strcmp(property.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0;
        });
        if (has_portability) {
            extensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
    }
    LOG_DEBUG("Instance exts for runtime: {}", JoinNames(extensions));

    const auto extension_names = extensions | std::views::transform(&std::string::c_str) | std::ranges::to<std::vector>();

    const VkApplicationInfo application_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .applicationVersion = 0,
        .engineVersion = 0,
        .apiVersion = VK_API_VERSION_1_3,
    };
    const VkInstanceCreateInfo instance_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags = flags,
        .pApplicationInfo = &application_info,
        .enabledExtensionCount = static_cast<uint32_t>(extension_names.size()),
        .ppEnabledExtensionNames = extension_names.data(),
    };

    if (create_info.create_instance) {
        ASSIGN_OR_RETURN(instance_, create_info.create_instance(instance_info));
        return {};
    }
    if (const auto result = vkCreateInstance(&instance_info, nullptr, &instance_); result != VK_SUCCESS) {
        return Unexpected(std::format("vkCreateInstance failed: {}", static_cast<int>(result)));
    }
    return {};
}

auto Device::SelectPhysicalDevice(const DeviceCreateInfo& create_info) -> Result<> {
    const auto device_extensions_for = [&](VkPhysicalDevice physical_device) -> std::vector<std::string> {
        auto extensions = create_info.required_device_extensions ? create_info.required_device_extensions(physical_device)
                                                                 : std::vector<std::string>{};
        const auto supported = SupportedDeviceExtensions(physical_device);
        if (std::ranges::all_of(kDmabufExtensions, [&](const char* name) -> bool { return HasExtension(supported, name); })) {
            for (const char* name : kDmabufExtensions) {
                if (!HasExtension(extensions, name)) {
                    extensions.emplace_back(name);
                }
            }
            if (HasExtension(supported, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME)) {
                extensions.emplace_back(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
            }
        }
        if (HasExtension(supported, VK_EXT_FILTER_CUBIC_EXTENSION_NAME)) {
            extensions.emplace_back(VK_EXT_FILTER_CUBIC_EXTENSION_NAME);
        }
        return extensions;
    };

    if (create_info.select_physical_device) {
        ASSIGN_OR_RETURN(physical_device_, create_info.select_physical_device(instance_));
        auto layout = QueueFamilyLayout(physical_device_);
        if (!layout) {
            return Unexpected("vkPhysicalDevice does not have a GRAPHICS / TRANSFER queue.");
        }
        queue_families_ = std::move(*layout);
        device_extensions_ = device_extensions_for(physical_device_);
    } else {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance_, &count, nullptr);
        std::vector<VkPhysicalDevice> physical_devices(count);
        vkEnumeratePhysicalDevices(instance_, &count, physical_devices.data());

        std::optional<uint32_t> best_score;
        for (const auto physical_device : physical_devices) {
            const auto supported = SupportedDeviceExtensions(physical_device);
            const auto required = create_info.required_device_extensions ? create_info.required_device_extensions(physical_device)
                                                                         : std::vector<std::string>{};
            const auto missing = required
                               | std::views::filter([&](const std::string& name) -> bool { return !HasExtension(supported, name); })
                               | std::ranges::to<std::vector<std::string>>();
            if (!missing.empty()) {
                LOG_DEBUG("Not using {} due to missing extensions:", DeviceName(physical_device));
                for (const auto& name : missing) {
                    LOG_DEBUG("  {}", name);
                }
                continue;
            }

            auto layout = QueueFamilyLayout(physical_device);
            if (!layout) {
                continue;
            }

            auto extensions = device_extensions_for(physical_device);
            LOG_DEBUG("Device exts for {}: {}", DeviceName(physical_device), JoinNames(extensions));

            const auto family_priority = (layout->size() == 2 || layout->size() == 3) ? 0U : 1U;
            const auto score = DeviceTypePriority(physical_device) * 10 + family_priority;
            if (!best_score || score < *best_score) {
                best_score = score;
                physical_device_ = physical_device;
                queue_families_ = std::move(*layout);
                device_extensions_ = std::move(extensions);
            }
        }
        if (!best_score) {
            FatalError("no suitable physical device found");
        }
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device_, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_3) {
        FatalError("Vulkan physical device doesn't support Vulkan 1.3");
    }
    LOG_INFO("Using vkPhysicalDevice: {}", properties.deviceName);

    supports_dmabuf_ = HasExtension(device_extensions_, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
    supports_drm_modifiers_ = HasExtension(device_extensions_, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
    texture_filter_ = HasExtension(device_extensions_, VK_EXT_FILTER_CUBIC_EXTENSION_NAME) ? VK_FILTER_CUBIC_EXT : VK_FILTER_LINEAR;
    return {};
}

auto Device::CreateLogicalDevice(const DeviceCreateInfo& create_info) -> Result<> {
    LOG_DEBUG("Selected GPU queue families: {}", queue_families_.size());

    const std::vector<float> priorities(3, 1.0f);
    const auto queue_infos = queue_families_
                           | std::views::transform([&](const QueueFamilyRequest& request) -> VkDeviceQueueCreateInfo {
                                 return {
                                     .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                     .queueFamilyIndex = request.family,
                                     .queueCount = request.count,
                                     .pQueuePriorities = priorities.data(),
                                 };
                             })
                           | std::ranges::to<std::vector>();

    const auto extension_names = device_extensions_ | std::views::transform(&std::string::c_str) | std::ranges::to<std::vector>();

    VkPhysicalDeviceVulkan13Features vulkan13_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE,
    };
    const VkDeviceCreateInfo device_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vulkan13_features,
        .queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size()),
        .pQueueCreateInfos = queue_infos.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extension_names.size()),
        .ppEnabledExtensionNames = extension_names.data(),
    };

    if (create_info.create_device) {
        ASSIGN_OR_RETURN(device_, create_info.create_device(physical_device_, device_info));
    } else if (const auto result = vkCreateDevice(physical_device_, &device_info, nullptr, &device_); result != VK_SUCCESS) {
        return Unexpected(std::format("vkCreateDevice failed: {}", static_cast<int>(result)));
    }

    LOG_DEBUG("  DMA-buf supported: {}", supports_dmabuf_);
    LOG_DEBUG("  DRM format modifiers supported: {}", supports_drm_modifiers_);

    std::vector<std::unique_ptr<Queue>> queues;
    for (const auto& request : queue_families_) {
        for (uint32_t index = 0; index < request.count; ++index) {
            VkQueue queue = VK_NULL_HANDLE;
            vkGetDeviceQueue(device_, request.family, index, &queue);
            queues.push_back(std::make_unique<Queue>(queue, request.family));
        }
    }

    if (queues.empty()) {
        return Unexpected("The selected device exposes no queues");
    }
    graphics_queue_ = std::move(queues.front());
    transfer_queue_ = graphics_queue_.get();
    if (queues.size() == 3) {
        transfer_queue_owned_ = std::move(queues[1]);
        transfer_queue_ = transfer_queue_owned_.get();
        capture_queue_ = std::move(queues.back());
    } else if (queues.size() == 2) {
        capture_queue_ = std::move(queues.back());
    }
    return {};
}

auto Device::CreateAllocator() -> Result<> {
    const VmaAllocatorCreateInfo allocator_info{
        .physicalDevice = physical_device_,
        .device = device_,
        .instance = instance_,
        .vulkanApiVersion = VK_API_VERSION_1_3,
    };
    if (const auto result = vmaCreateAllocator(&allocator_info, &allocator_); result != VK_SUCCESS) {
        return Unexpected(std::format("vmaCreateAllocator failed: {}", static_cast<int>(result)));
    }
    return {};
}

auto Device::CreateQuad() -> Result<> {
    const std::shared_ptr<Device> self(std::shared_ptr<Device>{}, this);

    constexpr std::array kVertices{
        Vertex{.position = {0.0f, 0.0f}, .uv = {0.0f, 0.0f}},
        Vertex{.position = {0.0f, 1.0f}, .uv = {0.0f, 1.0f}},
        Vertex{.position = {1.0f, 0.0f}, .uv = {1.0f, 0.0f}},
        Vertex{.position = {1.0f, 1.0f}, .uv = {1.0f, 1.0f}},
    };
    constexpr std::array<uint16_t, 6> kIndices{2, 1, 0, 1, 2, 3};

    ASSIGN_OR_RETURN(quad_vertices_, Buffer::Create(self, sizeof(kVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
    quad_vertices_->Write(std::as_bytes(std::span(kVertices)));
    ASSIGN_OR_RETURN(quad_indices_, Buffer::Create(self, sizeof(kIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
    quad_indices_->Write(std::as_bytes(std::span(kIndices)));
    return {};
}

auto Device::QueryDrmFormats() -> void {
    constexpr std::array kCandidates{
        kDrmFormatAbgr8888,
        kDrmFormatXbgr8888,
        kDrmFormatArgb8888,
        kDrmFormatXrgb8888,
        kDrmFormatAbgr2101010,
        kDrmFormatXbgr2101010,
    };

    for (const auto fourcc : kCandidates) {
        const auto format = FourccToFormat(fourcc);
        if (!format) {
            continue;
        }

        VkDrmFormatModifierPropertiesListEXT modifier_list{.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT};
        VkFormatProperties2 properties{
            .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
            .pNext = supports_drm_modifiers_ ? &modifier_list : nullptr,
        };
        vkGetPhysicalDeviceFormatProperties2(physical_device_, *format, &properties);

        std::vector<VkDrmFormatModifierPropertiesEXT> modifiers(modifier_list.drmFormatModifierCount);
        if (supports_drm_modifiers_ && !modifiers.empty()) {
            modifier_list.pDrmFormatModifierProperties = modifiers.data();
            vkGetPhysicalDeviceFormatProperties2(physical_device_, *format, &properties);
        }

        DrmFormat drm_format{.fourcc = fourcc};
        for (const auto& modifier : modifiers) {
            if (modifier.drmFormatModifierPlaneCount == 1) {
                drm_format.modifiers.push_back(modifier.drmFormatModifier);
            }
        }
        drm_format.modifiers.push_back(kDrmFormatModInvalid);
        drm_formats_.push_back(std::move(drm_format));
    }

    LOG_DEBUG("Supported DRM formats:");
    for (const auto& format : drm_formats_) {
        LOG_DEBUG("  {} {}", FourccToString(format.fourcc), format.modifiers.size());
    }
}

Device::~Device() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    vkDeviceWaitIdle(device_);
    CollectFinished();
    quad_vertices_.reset();
    quad_indices_.reset();
    for (const auto& [family, pool] : free_command_pools_) {
        vkDestroyCommandPool(device_, pool, nullptr);
    }
    for (const auto pool : descriptor_pools_) {
        vkDestroyDescriptorPool(device_, pool, nullptr);
    }
    for (const auto& [filter, sampler] : samplers_) {
        vkDestroySampler(device_, sampler, nullptr);
    }
    vmaDestroyAllocator(allocator_);
    vkDestroyDevice(device_, nullptr);
    if (owns_instance_) {
        vkDestroyInstance(instance_, nullptr);
    }
}

auto Device::CreateCommandBuffer(Queue& queue) -> Result<CommandBuffer> {
    ASSIGN_OR_RETURN(const auto pool, AcquireCommandPool(queue.Family()));

    const VkCommandBufferAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    if (const auto result = vkAllocateCommandBuffers(device_, &allocate_info, &command_buffer); result != VK_SUCCESS) {
        ReleaseCommandPool(queue.Family(), pool);
        return Unexpected(std::format("vkAllocateCommandBuffers failed: {}", static_cast<int>(result)));
    }

    const VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(command_buffer, &begin_info);
    return CommandBuffer(shared_from_this(), queue, pool, command_buffer);
}

auto Device::CreatePipeline(const ShaderProgram& program, VkFormat format, Blend blend) -> Result<std::shared_ptr<Pipeline>> {
    const auto create_module = [&](std::span<const unsigned char> code) -> Result<VkShaderModule> {
        const VkShaderModuleCreateInfo module_info{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = code.size(),
            .pCode = reinterpret_cast<const uint32_t*>(code.data()),
        };
        VkShaderModule module = VK_NULL_HANDLE;
        if (const auto result = vkCreateShaderModule(device_, &module_info, nullptr, &module); result != VK_SUCCESS) {
            return Unexpected(std::format("vkCreateShaderModule failed: {}", static_cast<int>(result)));
        }
        return module;
    };

    std::vector<VkDescriptorSetLayout> set_layouts;
    for (const auto kind : program.descriptor_sets) {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        if (kind == DescriptorKind::Texture) {
            bindings.push_back(
                {.binding = 0,
                 .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                 .descriptorCount = 1,
                 .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT}
            );
            bindings.push_back(
                {.binding = 1,
                 .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                 .descriptorCount = 1,
                 .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT}
            );
        } else {
            bindings.push_back(
                {.binding = 0,
                 .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                 .descriptorCount = 1,
                 .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT}
            );
        }
        const VkDescriptorSetLayoutCreateInfo layout_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data(),
        };
        VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
        vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &set_layout);
        set_layouts.push_back(set_layout);
    }

    const VkPipelineLayoutCreateInfo pipeline_layout_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<uint32_t>(set_layouts.size()),
        .pSetLayouts = set_layouts.data(),
    };
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    vkCreatePipelineLayout(device_, &pipeline_layout_info, nullptr, &pipeline_layout);

    ASSIGN_OR_RETURN(const auto vertex_module, create_module(program.vertex));
    ASSIGN_OR_RETURN(const auto fragment_module, create_module(program.fragment));

    const std::array stages{
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex_module,
            .pName = "main",
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragment_module,
            .pName = "main",
        },
    };

    const VkVertexInputBindingDescription vertex_binding{
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const std::array vertex_attributes{
        VkVertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(Vertex, position),
        },
        VkVertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(Vertex, uv),
        },
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vertex_binding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_attributes.size()),
        .pVertexAttributeDescriptions = vertex_attributes.data(),
    };
    const VkPipelineInputAssemblyStateCreateInfo input_assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkRect2D scissor{.offset = {0, 0}, .extent = {INT32_MAX, INT32_MAX}};
    const VkPipelineViewportStateCreateInfo viewport_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
        .pScissors = &scissor,
    };
    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineColorBlendAttachmentState blend_attachment{
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    switch (blend) {
    case Blend::Disabled: break;
    case Blend::SquareSource:
        blend_attachment.blendEnable = VK_TRUE;
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    case Blend::Alpha:
        blend_attachment.blendEnable = VK_TRUE;
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    case Blend::AlphaMaxCoverage:
        blend_attachment.blendEnable = VK_TRUE;
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.alphaBlendOp = VK_BLEND_OP_MAX;
        break;
    }
    const VkPipelineColorBlendStateCreateInfo color_blend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };

    constexpr VkDynamicState kDynamicState = VK_DYNAMIC_STATE_VIEWPORT;
    const VkPipelineDynamicStateCreateInfo dynamic_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 1,
        .pDynamicStates = &kDynamicState,
    };
    const VkPipelineRenderingCreateInfo rendering_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &format,
    };
    const VkGraphicsPipelineCreateInfo pipeline_info{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_info,
        .stageCount = static_cast<uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pColorBlendState = &color_blend,
        .pDynamicState = &dynamic_state,
        .layout = pipeline_layout,
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    const auto result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
    vkDestroyShaderModule(device_, vertex_module, nullptr);
    vkDestroyShaderModule(device_, fragment_module, nullptr);
    if (result != VK_SUCCESS) {
        return Unexpected(std::format("vkCreateGraphicsPipelines failed: {}", static_cast<int>(result)));
    }
    return std::make_shared<Pipeline>(shared_from_this(), pipeline, pipeline_layout, std::move(set_layouts), format);
}

auto Device::CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageLayout final_layout)
    -> Result<std::shared_ptr<Image>> {
    std::set<uint32_t> families;
    families.insert(graphics_queue_->Family());
    families.insert(transfer_queue_->Family());
    if (capture_queue_) {
        families.insert(capture_queue_->Family());
    }
    const std::vector<uint32_t> family_list(families.begin(), families.end());

    const VkImageCreateInfo image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = family_list.size() > 1 ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = family_list.size() > 1 ? static_cast<uint32_t>(family_list.size()) : 0,
        .pQueueFamilyIndices = family_list.size() > 1 ? family_list.data() : nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo allocation_info{.usage = VMA_MEMORY_USAGE_AUTO};

    ImageResources resources{.device = shared_from_this()};
    if (const auto
            result = vmaCreateImage(allocator_, &image_info, &allocation_info, &resources.image, &resources.allocation, nullptr);
        result != VK_SUCCESS) {
        return Unexpected(std::format("Failed to create {}x{} image: {}", width, height, static_cast<int>(result)));
    }

    const VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = resources.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
    };
    vkCreateImageView(device_, &view_info, nullptr, &resources.view);

    return std::make_shared<Image>(
        resources,
        ImageInfo{
            .format = format,
            .extent = {width, height},
            .usage = usage,
            .initial_layout = VK_IMAGE_LAYOUT_UNDEFINED,
            .final_layout = final_layout,
        }
    );
}

auto Device::CreateRenderTexture(uint32_t width, uint32_t height, VkFormat format, VkImageLayout final_layout)
    -> Result<std::shared_ptr<Image>> {
    LOG_DEBUG("Render texture: {}x{} {}MB", width, height, (width * height * 4) / (1024 * 1024));
    return CreateImage(
        width,
        height,
        format,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        final_layout
    );
}

auto Device::WrapImage(VkImage image, VkFormat format, VkExtent2D extent, VkImageLayout final_layout)
    -> Result<std::shared_ptr<Image>> {
    ImageResources resources{.device = shared_from_this(), .image = image, .owns_image = false};
    const VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
    };
    if (const auto result = vkCreateImageView(device_, &view_info, nullptr, &resources.view); result != VK_SUCCESS) {
        return Unexpected(std::format("vkCreateImageView failed: {}", static_cast<int>(result)));
    }
    return std::make_shared<Image>(
        resources,
        ImageInfo{
            .format = format,
            .extent = extent,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .initial_layout = VK_IMAGE_LAYOUT_UNDEFINED,
            .final_layout = final_layout,
        }
    );
}

auto Device::ImportDmabuf(const DmabufFrame& frame) -> Result<std::shared_ptr<Image>> {
    if (frame.format.modifier == kDrmFormatModInvalid) {
        return ImportDmabuf(frame, {});
    }
    const std::vector<uint64_t> modifiers(frame.planes.size(), frame.format.modifier);
    return ImportDmabuf(frame, modifiers);
}

auto Device::ImportDmabuf(const DmabufFrame& frame, std::span<const uint64_t> modifiers) -> Result<std::shared_ptr<Image>> {
    ASSIGN_OR_RETURN(const auto format, FourccToFormat(frame.format.fourcc));
    if (frame.planes.empty() || frame.planes.front().fd < 0) {
        return Unexpected("DMA-buf plane has no FD");
    }

    const auto plane_layouts = frame.planes | std::views::transform([](const FramePlane& plane) -> VkSubresourceLayout {
                                   return {.offset = plane.offset, .size = 0, .rowPitch = static_cast<VkDeviceSize>(plane.stride)};
                               })
                             | std::ranges::to<std::vector>();

    const VkExternalMemoryImageCreateInfo external_info{
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };
    VkImageDrmFormatModifierExplicitCreateInfoEXT explicit_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
        .pNext = &external_info,
        .drmFormatModifier = modifiers.empty() ? 0 : modifiers.front(),
        .drmFormatModifierPlaneCount = static_cast<uint32_t>(plane_layouts.size()),
        .pPlaneLayouts = plane_layouts.data(),
    };
    VkImageDrmFormatModifierListCreateInfoEXT list_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT,
        .pNext = &external_info,
        .drmFormatModifierCount = static_cast<uint32_t>(modifiers.size()),
        .pDrmFormatModifiers = modifiers.data(),
    };

    const void* next = &external_info;
    auto tiling = VK_IMAGE_TILING_OPTIMAL;
    if (modifiers.size() == 1) {
        next = &explicit_info;
        tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    } else if (modifiers.size() > 1) {
        next = &list_info;
        tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    }

    const VkImageCreateInfo image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = next,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {frame.format.width, frame.format.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = tiling,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    ImageResources resources{.device = shared_from_this()};
    if (const auto result = vkCreateImage(device_, &image_info, nullptr, &resources.image); result != VK_SUCCESS) {
        return Unexpected(std::format("vkCreateImage for DMA-buf failed: {}", static_cast<int>(result)));
    }
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, resources.image, &requirements);

    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties);
    std::optional<uint32_t> memory_type;
    for (uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
        if ((requirements.memoryTypeBits & (1U << index)) != 0
            && (memory_properties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
            memory_type = index;
            break;
        }
    }
    if (!memory_type) {
        vkDestroyImage(device_, resources.image, nullptr);
        return Unexpected("failed to get memory type index");
    }

    const int fd = dup(frame.planes.front().fd);
    if (fd < 0) {
        vkDestroyImage(device_, resources.image, nullptr);
        return Unexpected("Failed to duplicate DMA-buf FD");
    }
    const VkMemoryDedicatedAllocateInfo dedicated_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .image = resources.image,
    };
    const VkImportMemoryFdInfoKHR import_info{
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        .pNext = &dedicated_info,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        .fd = fd,
    };
    const VkMemoryAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &import_info,
        .allocationSize = requirements.size,
        .memoryTypeIndex = *memory_type,
    };

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (const auto result = vkAllocateMemory(device_, &allocate_info, nullptr, &memory); result != VK_SUCCESS) {
        close(fd);
        vkDestroyImage(device_, resources.image, nullptr);
        return Unexpected(std::format("Failed to import DMA-buf memory: {}", static_cast<int>(result)));
    }
    if (const auto result = vkBindImageMemory(device_, resources.image, memory, 0); result != VK_SUCCESS) {
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyImage(device_, resources.image, nullptr);
        return Unexpected(std::format("Failed to bind memory to image: {}", static_cast<int>(result)));
    }

    resources.imported_memory = memory;
    const VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = resources.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
    };
    vkCreateImageView(device_, &view_info, nullptr, &resources.view);

    return std::make_shared<Image>(
        resources,
        ImageInfo{
            .format = format,
            .extent = {frame.format.width, frame.format.height},
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
            .initial_layout = VK_IMAGE_LAYOUT_UNDEFINED,
            .final_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        }
    );
}

auto Device::UploadVertices(VkExtent2D canvas, float x, float y, float width, float height) -> Result<std::shared_ptr<Buffer>> {
    const auto canvas_width = static_cast<float>(canvas.width);
    const auto canvas_height = static_cast<float>(canvas.height);
    const auto x0 = x / canvas_width;
    const auto y0 = y / canvas_height;
    const auto x1 = width / canvas_width + x0;
    const auto y1 = height / canvas_height + y0;

    const std::array vertices{
        Vertex{.position = {x0, y0}, .uv = {0.0f, 0.0f}},
        Vertex{.position = {x0, y1}, .uv = {0.0f, 1.0f}},
        Vertex{.position = {x1, y0}, .uv = {1.0f, 0.0f}},
        Vertex{.position = {x1, y1}, .uv = {1.0f, 1.0f}},
    };
    ASSIGN_OR_RETURN(auto buffer, Buffer::Create(shared_from_this(), sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
    buffer->Write(std::as_bytes(std::span(vertices)));
    return buffer;
}

auto Device::Sampler(VkFilter filter) -> VkSampler {
    std::scoped_lock lock(sampler_mutex_);
    if (const auto it = samplers_.find(filter); it != samplers_.end()) {
        return it->second;
    }
    const VkSamplerCreateInfo sampler_info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = filter,
        .minFilter = filter,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .minLod = 0.0f,
        .maxLod = 0.0f,
    };
    VkSampler sampler = VK_NULL_HANDLE;
    vkCreateSampler(device_, &sampler_info, nullptr, &sampler);
    samplers_.emplace(filter, sampler);
    return sampler;
}

auto Device::AllocateDescriptorSet(VkDescriptorSetLayout layout) -> Result<std::pair<VkDescriptorPool, VkDescriptorSet>> {
    std::scoped_lock lock(descriptor_mutex_);
    for (const auto pool : descriptor_pools_ | std::views::reverse) {
        const VkDescriptorSetAllocateInfo allocate_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &layout,
        };
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(device_, &allocate_info, &set) == VK_SUCCESS) {
            return std::pair{pool, set};
        }
    }

    const std::array pool_sizes{
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = kDescriptorPoolSize},
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = kDescriptorPoolSize},
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = kDescriptorPoolSize},
    };
    const VkDescriptorPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = kDescriptorPoolSize,
        .poolSizeCount = static_cast<uint32_t>(pool_sizes.size()),
        .pPoolSizes = pool_sizes.data(),
    };
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (const auto result = vkCreateDescriptorPool(device_, &pool_info, nullptr, &pool); result != VK_SUCCESS) {
        return Unexpected(std::format("vkCreateDescriptorPool failed: {}", static_cast<int>(result)));
    }
    descriptor_pools_.push_back(pool);

    const VkDescriptorSetAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &layout,
    };
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (const auto result = vkAllocateDescriptorSets(device_, &allocate_info, &set); result != VK_SUCCESS) {
        return Unexpected(std::format("vkAllocateDescriptorSets failed: {}", static_cast<int>(result)));
    }
    return std::pair{pool, set};
}

auto Device::FreeDescriptorSet(VkDescriptorPool pool, VkDescriptorSet set) -> void {
    std::scoped_lock lock(descriptor_mutex_);
    vkFreeDescriptorSets(device_, pool, 1, &set);
}

auto Device::AcquireCommandPool(uint32_t family) -> Result<VkCommandPool> {
    {
        std::scoped_lock lock(submission_mutex_);
        if (const auto it = free_command_pools_.find(family); it != free_command_pools_.end()) {
            const auto pool = it->second;
            free_command_pools_.erase(it);
            return pool;
        }
    }
    const VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = family,
    };
    VkCommandPool pool = VK_NULL_HANDLE;
    if (const auto result = vkCreateCommandPool(device_, &pool_info, nullptr, &pool); result != VK_SUCCESS) {
        return Unexpected(std::format("vkCreateCommandPool failed: {}", static_cast<int>(result)));
    }
    return pool;
}

auto Device::ReleaseCommandPool(uint32_t family, VkCommandPool pool) -> void {
    vkResetCommandPool(device_, pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
    std::scoped_lock lock(submission_mutex_);
    free_command_pools_.emplace(family, pool);
}

auto Device::Submit(Queue& queue, std::span<const VkCommandBuffer> command_buffers, Submission submission, bool wait) -> Result<> {
    CollectFinished();

    const VkFenceCreateInfo fence_info{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (const auto result = vkCreateFence(device_, &fence_info, nullptr, &submission.fence); result != VK_SUCCESS) {
        return Unexpected(std::format("vkCreateFence failed: {}", static_cast<int>(result)));
    }

    if (auto result = queue.Submit(command_buffers, submission.fence); !result) {
        vkDestroyFence(device_, submission.fence, nullptr);
        for (const auto& [family, pool] : submission.pools) {
            ReleaseCommandPool(family, pool);
        }
        return result;
    }

    if (wait) {
        vkWaitForFences(device_, 1, &submission.fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(device_, submission.fence, nullptr);
        for (const auto& [family, pool] : submission.pools) {
            ReleaseCommandPool(family, pool);
        }
        return {};
    }

    std::scoped_lock lock(submission_mutex_);
    in_flight_.push_back(std::move(submission));
    return {};
}

auto Device::CollectFinished() -> void {
    std::vector<Submission> finished;
    {
        std::scoped_lock lock(submission_mutex_);
        const auto done = std::ranges::partition(in_flight_, [&](const Submission& submission) -> bool {
            return vkGetFenceStatus(device_, submission.fence) != VK_SUCCESS;
        });
        std::ranges::move(done, std::back_inserter(finished));
        in_flight_.erase(done.begin(), done.end());
    }
    for (auto& submission : finished) {
        vkDestroyFence(device_, submission.fence, nullptr);
        for (const auto& [family, pool] : submission.pools) {
            ReleaseCommandPool(family, pool);
        }
    }
}
