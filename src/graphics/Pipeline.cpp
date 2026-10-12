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

#include "Pipeline.hpp"

#include <array>
#include <format>

#include "Device.hpp"

Pipeline::Pipeline(VkPipeline pipeline, VkPipelineLayout layout, std::vector<VkDescriptorSetLayout> set_layouts, VkFormat format)
    : m_pipeline(pipeline),
      m_layout(layout),
      m_set_layouts(std::move(set_layouts)),
      m_format(format) {
}

Pipeline::~Pipeline() {
    const auto device = g_device->Handle();
    vkDestroyPipeline(device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(device, m_layout, nullptr);
    for (const auto set_layout : m_set_layouts) {
        vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
    }
}

auto Pipeline::AllocateSet(uint32_t set) -> Result<std::shared_ptr<DescriptorSet>> {
    if (set >= m_set_layouts.size()) {
        return Unexpected(std::format("Pipeline has no descriptor set {}", set));
    }
    ASSIGN_OR_RETURN(const auto allocation, g_device->AllocateDescriptorSet(m_set_layouts[set]));
    return std::make_shared<DescriptorSet>(allocation.first, allocation.second);
}

auto Pipeline::CreateTextureSet(uint32_t set, std::shared_ptr<Image> texture, VkFilter filter)
    -> Result<std::shared_ptr<DescriptorSet>> {
    ASSIGN_OR_RETURN(auto descriptor_set, AllocateSet(set));

    const VkDescriptorImageInfo image_info{
        .imageView = texture->View(),
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    const VkDescriptorImageInfo sampler_info{
        .sampler = g_device->Sampler(filter),
    };
    const std::array writes{
        VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_set->Handle(),
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &image_info,
        },
        VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_set->Handle(),
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .pImageInfo = &sampler_info,
        },
    };
    vkUpdateDescriptorSets(g_device->Handle(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    descriptor_set->BindTexture(std::move(texture));
    return descriptor_set;
}

auto Pipeline::CreateUniformSet(uint32_t set, std::span<const float> data) -> Result<std::shared_ptr<DescriptorSet>> {
    ASSIGN_OR_RETURN(auto descriptor_set, AllocateSet(set));
    ASSIGN_OR_RETURN(auto buffer, Buffer::Create(data.size_bytes(), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT));
    buffer->Write(std::as_bytes(data));

    const VkDescriptorBufferInfo buffer_info{
        .buffer = buffer->Handle(),
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptor_set->Handle(),
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &buffer_info,
    };
    vkUpdateDescriptorSets(g_device->Handle(), 1, &write, 0, nullptr);

    descriptor_set->BindBuffer(std::move(buffer));
    return descriptor_set;
}

auto Pipeline::CreatePass(
    VkExtent2D viewport,
    std::shared_ptr<Buffer> vertices,
    std::vector<std::shared_ptr<DescriptorSet>> descriptor_sets
) -> Pass {
    return Pass{
        .pipeline = shared_from_this(),
        .viewport = viewport,
        .vertices = std::move(vertices),
        .descriptor_sets = std::move(descriptor_sets),
    };
}

auto Pipeline::CreatePassForTarget(const Image& target, std::vector<std::shared_ptr<DescriptorSet>> descriptor_sets) -> Pass {
    return CreatePass(target.Extent(), g_device->QuadVertices(), std::move(descriptor_sets));
}
