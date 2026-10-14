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

CommandBuffer::CommandBuffer(Queue& queue, VkCommandPool pool, VkCommandBuffer command_buffer)
    : m_queue(&queue),
      m_pool(pool),
      m_command_buffer(command_buffer)
{
}

CommandBuffer::~CommandBuffer()
{
    if (m_pool != VK_NULL_HANDLE)
    {
        g_device->ReleaseCommandPool(m_queue->Family(), m_pool);
    }
}

CommandBuffer::CommandBuffer(CommandBuffer&& other) noexcept
    : m_queue(other.m_queue),
      m_pool(std::exchange(other.m_pool, VK_NULL_HANDLE)),
      m_command_buffer(std::exchange(other.m_command_buffer, VK_NULL_HANDLE)),
      m_retained(std::move(other.m_retained)),
      m_render_target(std::move(other.m_render_target)),
      m_pending_passes(std::move(other.m_pending_passes))
{
}

auto CommandBuffer::operator=(CommandBuffer&& other) noexcept -> CommandBuffer&
{
    if (this != &other)
    {
        if (m_pool != VK_NULL_HANDLE)
        {
            g_device->ReleaseCommandPool(m_queue->Family(), m_pool);
        }
        m_queue = other.m_queue;
        m_pool = std::exchange(other.m_pool, VK_NULL_HANDLE);
        m_command_buffer = std::exchange(other.m_command_buffer, VK_NULL_HANDLE);
        m_retained = std::move(other.m_retained);
        m_render_target = std::move(other.m_render_target);
        m_pending_passes = std::move(other.m_pending_passes);
    }
    return *this;
}

auto CommandBuffer::Retain(std::shared_ptr<void> resource) -> void
{
    m_retained.push_back(std::move(resource));
}

auto CommandBuffer::Barrier(Image& image, VkImageLayout layout) -> void
{
    const auto old_layout = image.Layout();
    if (old_layout == layout)
    {
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
        m_command_buffer,
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

auto CommandBuffer::BeginRendering(std::shared_ptr<Image> target) -> void
{
    m_render_target = std::move(target);
    m_pending_passes.clear();
}

auto CommandBuffer::Draw(const Pass& pass) -> void
{
    m_pending_passes.push_back(pass);
}

auto CommandBuffer::EndRendering() -> void
{
    auto target = std::exchange(m_render_target, nullptr);
    if (!target)
    {
        LOG_ERROR("EndRendering called without a render target");
        return;
    }

    for (const auto& pass : m_pending_passes)
    {
        for (const auto& descriptor_set : pass.descriptor_sets)
        {
            if (const auto& texture = descriptor_set->Texture())
            {
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
    vkCmdBeginRendering(m_command_buffer, &rendering_info);

    const auto& indices = g_device->QuadIndices();
    for (auto& pass : m_pending_passes)
    {
        const VkViewport viewport{
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(pass.viewport.width),
            .height = static_cast<float>(pass.viewport.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(m_command_buffer, 0, 1, &viewport);
        vkCmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.pipeline->Handle());

        std::vector<VkDescriptorSet> sets;
        sets.reserve(pass.descriptor_sets.size());
        for (const auto& descriptor_set : pass.descriptor_sets)
        {
            sets.push_back(descriptor_set->Handle());
            Retain(descriptor_set);
        }
        if (!sets.empty())
        {
            vkCmdBindDescriptorSets(
                m_command_buffer,
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
        vkCmdBindVertexBuffers(m_command_buffer, 0, 1, &vertex_buffer, &offset);
        vkCmdBindIndexBuffer(m_command_buffer, indices->Handle(), 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(m_command_buffer, 6, 1, 0, 0, 0);

        Retain(pass.pipeline);
        Retain(pass.vertices);
    }

    vkCmdEndRendering(m_command_buffer);
    Barrier(*target, target->FinalLayout());

    Retain(indices);
    Retain(target);
    m_pending_passes.clear();
}

auto CommandBuffer::UploadImage(
    uint32_t width,
    uint32_t height,
    VkFormat format,
    std::span<const std::byte> data,
    uint32_t row_length
) -> Result<std::shared_ptr<Image>>
{
    LOG_DEBUG("Texture2D: {}x{} {}MB", width, height, data.size() / (size_t{1024} * 1024));

    ASSIGN_OR_RETURN(
        auto image,
        g_device->CreateImage(
            width,
            height,
            format,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        )
    );
    ASSIGN_OR_RETURN(auto staging, Buffer::Create(data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
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
    vkCmdCopyBufferToImage(m_command_buffer, staging->Handle(), image->Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    Barrier(*image, image->FinalLayout());

    Retain(staging);
    Retain(image);
    return image;
}

auto CommandBuffer::UploadDds(std::span<const std::byte> data) -> Result<std::shared_ptr<Image>>
{
    ASSIGN_OR_RETURN(const auto dds, DdsImage::Parse(data));
    return UploadImage(dds.Width(), dds.Height(), dds.Format(), dds.Data());
}

auto CommandBuffer::TransitionLayout(const std::shared_ptr<Image>& image, VkImageLayout layout) -> void
{
    Barrier(*image, layout);
    Retain(image);
}

auto CommandBuffer::Finish() -> Result<>
{
    if (const auto result = vkEndCommandBuffer(m_command_buffer); result != VK_SUCCESS)
    {
        return Unexpected(std::format("vkEndCommandBuffer failed: {}", static_cast<int>(result)));
    }
    return {};
}

auto CommandBuffer::Submit() -> Result<>
{
    RETURN_IF_ERROR(Finish());
    Submission submission{
        .pools = {{m_queue->Family(), std::exchange(m_pool, VK_NULL_HANDLE)}},
        .retained = std::move(m_retained),
    };
    return g_device->Submit(*m_queue, {&m_command_buffer, 1}, std::move(submission), false);
}

auto CommandBuffer::SubmitAndWait() -> Result<>
{
    RETURN_IF_ERROR(Finish());
    Submission submission{
        .pools = {{m_queue->Family(), std::exchange(m_pool, VK_NULL_HANDLE)}},
        .retained = std::move(m_retained),
    };
    return g_device->Submit(*m_queue, {&m_command_buffer, 1}, std::move(submission), true);
}

auto CommandBuffer::SubmitUpload() -> Result<>
{
    if (m_queue == &g_device->GraphicsQueue())
    {
        return Submit();
    }
    return SubmitAndWait();
}
