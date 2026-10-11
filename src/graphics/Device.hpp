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

#include "Error.hpp"
#include "Frame.hpp"
#include "Buffer.hpp"
#include "CommandBuffer.hpp"
#include "Image.hpp"
#include "Queue.hpp"
#include "Pipeline.hpp"

struct DeviceCreateInfo {
    std::vector<std::string> instance_extensions;
    std::function<std::vector<std::string>(VkPhysicalDevice)> required_device_extensions;
    std::function<Result<VkInstance>(const VkInstanceCreateInfo&)> create_instance;
    std::function<Result<VkPhysicalDevice>(VkInstance)> select_physical_device;
    std::function<Result<VkDevice>(VkPhysicalDevice, const VkDeviceCreateInfo&)> create_device;
};

struct QueueFamilyRequest {
    uint32_t family = 0;
    uint32_t count = 0;
};

struct Submission {
    VkFence fence = VK_NULL_HANDLE;
    std::vector<std::pair<uint32_t, VkCommandPool>> pools;
    std::vector<std::shared_ptr<void>> retained;
};

class Device final : public std::enable_shared_from_this<Device> {
  public:
    static auto Create(const DeviceCreateInfo& create_info) -> Result<std::shared_ptr<Device>>;
    static auto FourccToFormat(uint32_t fourcc) -> Result<VkFormat>;

    ~Device();

    [[nodiscard]] auto Instance() const -> VkInstance { return instance_; }
    [[nodiscard]] auto PhysicalDevice() const -> VkPhysicalDevice { return physical_device_; }
    [[nodiscard]] auto Handle() const -> VkDevice { return device_; }
    [[nodiscard]] auto Allocator() const -> VmaAllocator { return allocator_; }
    [[nodiscard]] auto GraphicsQueue() -> Queue& { return *graphics_queue_; }
    [[nodiscard]] auto TransferQueue() -> Queue& { return *transfer_queue_; }
    [[nodiscard]] auto CaptureQueue() -> Queue* { return capture_queue_.get(); }
    [[nodiscard]] auto NativeFormat() const -> VkFormat { return native_format_; }
    [[nodiscard]] auto TextureFilter() const -> VkFilter { return texture_filter_; }
    [[nodiscard]] auto SupportsDmabuf() const -> bool { return supports_dmabuf_; }
    [[nodiscard]] auto DrmFormats() const -> std::span<const DrmFormat> { return drm_formats_; }
    [[nodiscard]] auto QuadVertices() const -> const std::shared_ptr<Buffer>& { return quad_vertices_; }
    [[nodiscard]] auto QuadIndices() const -> const std::shared_ptr<Buffer>& { return quad_indices_; }

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
    auto ReleaseCommandPool(uint32_t family, VkCommandPool pool) -> void;
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

    VkInstance instance_ = VK_NULL_HANDLE;
    VkFormat native_format_ = VK_FORMAT_R8G8B8A8_SRGB;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    bool owns_instance_ = true;
    bool supports_dmabuf_ = false;
    bool supports_drm_modifiers_ = false;
    VkFilter texture_filter_ = VK_FILTER_LINEAR;
    std::vector<std::string> device_extensions_;
    std::vector<QueueFamilyRequest> queue_families_;

    std::unique_ptr<Queue> graphics_queue_;
    std::unique_ptr<Queue> transfer_queue_owned_;
    std::unique_ptr<Queue> capture_queue_;
    Queue* transfer_queue_ = nullptr;

    std::vector<DrmFormat> drm_formats_;
    std::shared_ptr<Buffer> quad_vertices_;
    std::shared_ptr<Buffer> quad_indices_;

    std::mutex sampler_mutex_;
    std::map<VkFilter, VkSampler> samplers_;

    std::mutex descriptor_mutex_;
    std::vector<VkDescriptorPool> descriptor_pools_;

    std::mutex submission_mutex_;
    std::multimap<uint32_t, VkCommandPool> free_command_pools_;
    std::vector<Submission> in_flight_;
};
