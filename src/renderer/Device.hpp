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

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include "Buffer.hpp"
#include "CommandBuffer.hpp"
#include "Error.hpp"
#include "Frame.hpp"
#include "Image.hpp"
#include "Pipeline.hpp"
#include "Queue.hpp"

struct DeviceCreateInfo
{
    std::vector<std::string> instance_extensions;
    std::function<std::vector<std::string>(VkPhysicalDevice)> required_device_extensions;
    std::function<Result<VkInstance>(const VkInstanceCreateInfo&)> create_instance;
    std::function<Result<VkPhysicalDevice>(VkInstance)> select_physical_device;
    std::function<Result<VkDevice>(VkPhysicalDevice, const VkDeviceCreateInfo&)> create_device;
};

struct QueueFamilyRequest
{
    uint32_t family = 0;
    uint32_t count = 0;
};

struct PooledCommandBuffer
{
    uint32_t family = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
};

struct Submission
{
    VkFence fence = VK_NULL_HANDLE;
    std::vector<PooledCommandBuffer> pools;
    std::vector<std::shared_ptr<void>> retained;
};

class Device final
{
  public:
    static auto Create(const DeviceCreateInfo& create_info) -> Result<std::unique_ptr<Device>>;
    static auto FourccToFormat(uint32_t fourcc) -> Result<VkFormat>;

    ~Device();

    [[nodiscard]] auto Instance() const -> VkInstance { return m_instance; }
    [[nodiscard]] auto PhysicalDevice() const -> VkPhysicalDevice { return m_physical_device; }
    [[nodiscard]] auto Handle() const -> VkDevice { return m_device; }
    [[nodiscard]] auto Allocator() const -> VmaAllocator { return m_allocator; }
    [[nodiscard]] auto GraphicsQueue() -> Queue& { return *m_graphics_queue; }
    [[nodiscard]] auto TransferQueue() -> Queue& { return *m_transfer_queue; }
    [[nodiscard]] auto CaptureQueue() -> Queue* { return m_capture_queue.get(); }
    [[nodiscard]] auto NativeFormat() const -> VkFormat { return m_native_format; }
    [[nodiscard]] auto TextureFilter() const -> VkFilter { return m_texture_filter; }
    [[nodiscard]] auto SupportsDmabuf() const -> bool { return m_supports_dmabuf; }
    [[nodiscard]] auto DrmFormats() const -> std::span<const DrmFormat> { return m_drm_formats; }
    [[nodiscard]] auto QuadVertices() const -> const std::shared_ptr<Buffer>& { return m_quad_vertices; }
    [[nodiscard]] auto QuadIndices() const -> const std::shared_ptr<Buffer>& { return m_quad_indices; }

    auto CreateCommandBuffer(Queue& queue) -> Result<CommandBuffer>;
    auto CreatePipeline(const ShaderProgram& program, VkFormat format, Blend blend) -> Result<std::shared_ptr<Pipeline>>;
    auto CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageLayout final_layout)
        -> Result<std::shared_ptr<Image>>;
    auto CreateRenderTexture(uint32_t width, uint32_t height, VkFormat format, VkImageLayout final_layout)
        -> Result<std::shared_ptr<Image>>;
    auto WrapImage(VkImage image, VkFormat format, VkExtent2D extent, VkImageLayout final_layout) -> Result<std::shared_ptr<Image>>;
    auto ImportDmabuf(const DmabufFrame& frame) -> Result<std::shared_ptr<Image>>;
    auto ImportDmabuf(const DmabufFrame& frame, std::span<const uint64_t> modifiers) -> Result<std::shared_ptr<Image>>;
    auto UploadVertices(VkExtent2D canvas, float x, float y, float width, float height) -> Result<std::shared_ptr<Buffer>>;

    auto Sampler(VkFilter filter) -> VkSampler;
    auto AllocateDescriptorSet(VkDescriptorSetLayout layout) -> Result<std::pair<VkDescriptorPool, VkDescriptorSet>>;
    auto FreeDescriptorSet(VkDescriptorPool pool, VkDescriptorSet set) -> void;

    auto AcquireCommandPool(uint32_t family) -> Result<VkCommandPool>;
    auto ReleaseCommandPool(const PooledCommandBuffer& pooled) -> void;
    auto Submit(Queue& queue, std::span<const VkCommandBuffer> command_buffers, Submission submission, bool wait) -> Result<>;
    auto CollectFinished() -> void;

    Device(const Device&) = delete;
    auto operator=(const Device&) -> Device& = delete;

  private:
    Device() = default;

    auto Initialize(const DeviceCreateInfo& create_info) -> Result<>;
    auto CreateInstance(const DeviceCreateInfo& create_info) -> Result<>;
    auto SelectPhysicalDevice(const DeviceCreateInfo& create_info) -> Result<>;
    auto CreateLogicalDevice(const DeviceCreateInfo& create_info) -> Result<>;
    auto CreateAllocator() -> Result<>;
    auto CreateQuad() -> Result<>;
    auto QueryDrmFormats() -> void;

    static auto QueueFamilyLayout(VkPhysicalDevice physical_device) -> std::optional<std::vector<QueueFamilyRequest>>;
    static auto SupportedDeviceExtensions(VkPhysicalDevice physical_device) -> std::vector<std::string>;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkFormat m_native_format = VK_FORMAT_R8G8B8A8_SRGB;
    VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    bool m_owns_instance = true;
    bool m_supports_dmabuf = false;
    bool m_supports_drm_modifiers = false;
    VkFilter m_texture_filter = VK_FILTER_LINEAR;
    std::vector<std::string> m_device_extensions;
    std::vector<QueueFamilyRequest> m_queue_families;

    std::unique_ptr<Queue> m_graphics_queue;
    std::unique_ptr<Queue> m_transfer_queue_owned;
    std::unique_ptr<Queue> m_capture_queue;
    Queue* m_transfer_queue = nullptr;

    std::vector<DrmFormat> m_drm_formats;
    std::shared_ptr<Buffer> m_quad_vertices;
    std::shared_ptr<Buffer> m_quad_indices;

    std::mutex m_sampler_mutex;
    std::map<VkFilter, VkSampler> m_samplers;

    std::mutex m_descriptor_mutex;
    std::vector<VkDescriptorPool> m_descriptor_pools;

    std::mutex m_submission_mutex;
    std::multimap<uint32_t, VkCommandPool> m_free_command_pools;
    std::vector<Submission> m_in_flight;
};

extern Device* g_device;
