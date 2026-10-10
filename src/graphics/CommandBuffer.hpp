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
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include <vulkan/vulkan.h>

#include "Error.hpp"
#include "Image.hpp"
#include "Pipeline.hpp"
#include "Queue.hpp"

class Device;

class CommandBuffer final {
  public:
    CommandBuffer(std::shared_ptr<Device> device, Queue& queue, VkCommandPool pool, VkCommandBuffer command_buffer);
    ~CommandBuffer();

    CommandBuffer(CommandBuffer&& other) noexcept;
    auto operator=(CommandBuffer&& other) noexcept -> CommandBuffer&;
    CommandBuffer(const CommandBuffer&) = delete;
    auto operator=(const CommandBuffer&) -> CommandBuffer& = delete;

    [[nodiscard]] auto SubmitQueue() const -> Queue& { return *queue_; }

    auto BeginRendering(std::shared_ptr<Image> target) -> void;
    auto Draw(const Pass& pass) -> void;
    auto EndRendering() -> void;

    auto UploadImage(uint32_t width, uint32_t height, VkFormat format, std::span<const std::byte> data, uint32_t row_length = 0)
        -> Result<std::shared_ptr<Image>>;
    auto UploadDds(std::span<const std::byte> data) -> Result<std::shared_ptr<Image>>;
    auto TransitionLayout(const std::shared_ptr<Image>& image, VkImageLayout layout) -> void;

    auto Submit() -> Result<>;
    auto SubmitAndWait() -> Result<>;
    auto SubmitUpload() -> Result<>;

  private:
    friend class CommandBatch;

    auto Finish() -> Result<>;
    auto Barrier(Image& image, VkImageLayout layout) -> void;
    auto Retain(std::shared_ptr<void> resource) -> void;

    std::shared_ptr<Device> device_;
    Queue* queue_;
    VkCommandPool pool_;
    VkCommandBuffer command_buffer_;
    std::vector<std::shared_ptr<void>> retained_;
    std::shared_ptr<Image> render_target_;
    std::vector<Pass> pending_passes_;
};

class CommandBatch final {
  public:
    auto Push(CommandBuffer command_buffer) -> void;
    auto Submit() -> Result<>;

    [[nodiscard]] auto Empty() const -> bool { return command_buffers_.empty(); }

  private:
    std::vector<CommandBuffer> command_buffers_;
};
