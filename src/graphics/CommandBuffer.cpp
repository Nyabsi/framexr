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

#include "CommandBuffer.hpp"

#include <cstring>
#include <format>
#include <utility>

#include "DdsImage.hpp"
#include "Device.hpp"
#include "Logger.hpp"

CommandBuffer::CommandBuffer(std::shared_ptr<Device> device, Queue& queue, VkCommandPool pool, VkCommandBuffer command_buffer)
    : device_(std::move(device)),
      queue_(&queue),
      pool_(pool),
      command_buffer_(command_buffer) {
}

CommandBuffer::~CommandBuffer() {
    if (pool_ != VK_NULL_HANDLE) {
        device_->ReleaseCommandPool(queue_->Family(), pool_);
    }
}

CommandBuffer::CommandBuffer(CommandBuffer&& other) noexcept
    : device_(std::move(other.device_)),
      queue_(other.queue_),
      pool_(std::exchange(other.pool_, VK_NULL_HANDLE)),
      command_buffer_(std::exchange(other.command_buffer_, VK_NULL_HANDLE)),
      retained_(std::move(other.retained_)),
      render_target_(std::move(other.render_target_)),
      pending_passes_(std::move(other.pending_passes_)) {
}

auto CommandBuffer::operator=(CommandBuffer&& other) noexcept -> CommandBuffer& {
    if (this != &other) {
        if (pool_ != VK_NULL_HANDLE) {
            device_->ReleaseCommandPool(queue_->Family(), pool_);
        }
        device_ = std::move(other.device_);
        queue_ = other.queue_;
        pool_ = std::exchange(other.pool_, VK_NULL_HANDLE);
        command_buffer_ = std::exchange(other.command_buffer_, VK_NULL_HANDLE);
        retained_ = std::move(other.retained_);
        render_target_ = std::move(other.render_target_);
        pending_passes_ = std::move(other.pending_passes_);
    }
    return *this;
}

auto CommandBuffer::Retain(std::shared_ptr<void> resource) -> void {
    retained_.push_back(std::move(resource));
}

auto CommandBuffer::Barrier(Image& image, VkImageLayout layout) -> void {
    const auto old_layout = image.Layout();
    if (old_layout == layout) {
        return;
    }
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .oldLayout = old_layout,
        .newLayout = layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image.Handle(),
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(
        command_buffer_,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );
    image.SetLayout(layout);
}

auto CommandBuffer::BeginRendering(std::shared_ptr<Image> target) -> void {
    render_target_ = std::move(target);
    pending_passes_.clear();
}

auto CommandBuffer::Draw(const Pass& pass) -> void {
    pending_passes_.push_back(pass);
}

auto CommandBuffer::EndRendering() -> void {
    auto target = std::exchange(render_target_, nullptr);
    if (!target) {
        LOG_ERROR("EndRendering called without a render target");
        return;
    }

    for (const auto& pass : pending_passes_) {
        for (const auto& descriptor_set : pass.descriptor_sets) {
            if (const auto& texture = descriptor_set->Texture()) {
                Barrier(*texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }
    }
    Barrier(*target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const VkRenderingAttachmentInfo color_attachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = target->View(),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 0.0f}}},
    };
    const VkRenderingInfo rendering_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {0, 0}, .extent = target->Extent()},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment,
    };
    vkCmdBeginRendering(command_buffer_, &rendering_info);

    const auto& indices = device_->QuadIndices();
    for (auto& pass : pending_passes_) {
        const VkViewport viewport{
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(pass.viewport.width),
            .height = static_cast<float>(pass.viewport.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(command_buffer_, 0, 1, &viewport);
        vkCmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.pipeline->Handle());

        std::vector<VkDescriptorSet> sets;
        sets.reserve(pass.descriptor_sets.size());
        for (const auto& descriptor_set : pass.descriptor_sets) {
            sets.push_back(descriptor_set->Handle());
            Retain(descriptor_set);
        }
        if (!sets.empty()) {
            vkCmdBindDescriptorSets(
                command_buffer_,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pass.pipeline->Layout(),
                0,
                static_cast<uint32_t>(sets.size()),
                sets.data(),
                0,
                nullptr
            );
        }

        const VkBuffer vertex_buffer = pass.vertices->Handle();
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command_buffer_, 0, 1, &vertex_buffer, &offset);
        vkCmdBindIndexBuffer(command_buffer_, indices->Handle(), 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(command_buffer_, 6, 1, 0, 0, 0);

        Retain(pass.pipeline);
        Retain(pass.vertices);
    }

    vkCmdEndRendering(command_buffer_);
    Barrier(*target, target->FinalLayout());

    Retain(indices);
    Retain(target);
    pending_passes_.clear();
}

auto CommandBuffer::UploadImage(
    uint32_t width,
    uint32_t height,
    VkFormat format,
    std::span<const std::byte> data,
    uint32_t row_length
) -> Result<std::shared_ptr<Image>> {
    LOG_DEBUG("Texture2D: {}x{} {}MB", width, height, data.size() / (size_t{1024} * 1024));

    ASSIGN_OR_RETURN(
        auto image,
        device_->CreateImage(
            width,
            height,
            format,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        )
    );
    ASSIGN_OR_RETURN(auto staging, Buffer::Create(device_, data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
    staging->Write(data);

    Barrier(*image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkBufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = row_length,
        .bufferImageHeight = 0,
        .imageSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1},
    };
    vkCmdCopyBufferToImage(command_buffer_, staging->Handle(), image->Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    Barrier(*image, image->FinalLayout());

    Retain(staging);
    Retain(image);
    return image;
}

auto CommandBuffer::UploadDds(std::span<const std::byte> data) -> Result<std::shared_ptr<Image>> {
    ASSIGN_OR_RETURN(const auto dds, DdsImage::Parse(data));
    return UploadImage(dds.width, dds.height, dds.format, dds.data);
}

auto CommandBuffer::TransitionLayout(const std::shared_ptr<Image>& image, VkImageLayout layout) -> void {
    Barrier(*image, layout);
    Retain(image);
}

auto CommandBuffer::Finish() -> Result<> {
    if (const auto result = vkEndCommandBuffer(command_buffer_); result != VK_SUCCESS) {
        return Unexpected(std::format("vkEndCommandBuffer failed: {}", static_cast<int>(result)));
    }
    return {};
}

auto CommandBuffer::Submit() -> Result<> {
    RETURN_IF_ERROR(Finish());
    Submission submission{
        .pools = {{queue_->Family(), std::exchange(pool_, VK_NULL_HANDLE)}},
        .retained = std::move(retained_),
    };
    return device_->Submit(*queue_, {&command_buffer_, 1}, std::move(submission), false);
}

auto CommandBuffer::SubmitAndWait() -> Result<> {
    RETURN_IF_ERROR(Finish());
    Submission submission{
        .pools = {{queue_->Family(), std::exchange(pool_, VK_NULL_HANDLE)}},
        .retained = std::move(retained_),
    };
    return device_->Submit(*queue_, {&command_buffer_, 1}, std::move(submission), true);
}

auto CommandBuffer::SubmitUpload() -> Result<> {
    if (queue_ == &device_->GraphicsQueue()) {
        return Submit();
    }
    return SubmitAndWait();
}

auto CommandBatch::Push(CommandBuffer command_buffer) -> void {
    command_buffers_.push_back(std::move(command_buffer));
}

auto CommandBatch::Submit() -> Result<> {
    if (command_buffers_.empty()) {
        return {};
    }
    auto& queue = command_buffers_.front().SubmitQueue();
    auto device = command_buffers_.front().device_;

    std::vector<VkCommandBuffer> handles;
    Submission submission;
    for (auto& command_buffer : command_buffers_) {
        RETURN_IF_ERROR(command_buffer.Finish());
        handles.push_back(command_buffer.command_buffer_);
        submission.pools.emplace_back(command_buffer.queue_->Family(), std::exchange(command_buffer.pool_, VK_NULL_HANDLE));
        for (auto& resource : command_buffer.retained_) {
            submission.retained.push_back(std::move(resource));
        }
    }
    command_buffers_.clear();
    return device->Submit(queue, handles, std::move(submission), false);
}
