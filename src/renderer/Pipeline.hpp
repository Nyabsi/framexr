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

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <vulkan/vulkan.h>

#include "Buffer.hpp"
#include "DescriptorSet.hpp"
#include "Error.hpp"
#include "Image.hpp"

struct Vertex
{
    std::array<float, 2> position;
    std::array<float, 2> uv;
};

enum class Blend : uint8_t
{
    Disabled,
    SquareSource,
    Alpha,
    AlphaMaxCoverage,
};

enum class DescriptorKind : uint8_t
{
    Texture,
    Uniform,
};

struct ShaderProgram
{
    std::span<const unsigned char> vertex;
    std::span<const unsigned char> fragment;
    std::vector<DescriptorKind> descriptor_sets;
};

class Pipeline;

struct Pass
{
    std::shared_ptr<Pipeline> pipeline;
    VkExtent2D viewport{};
    std::shared_ptr<Buffer> vertices;
    std::vector<std::shared_ptr<DescriptorSet>> descriptor_sets;
};

class Pipeline final : public std::enable_shared_from_this<Pipeline>
{
  public:
    Pipeline(VkPipeline pipeline, VkPipelineLayout layout, std::vector<VkDescriptorSetLayout> set_layouts, VkFormat format);
    ~Pipeline();

    [[nodiscard]] auto Handle() const -> VkPipeline { return m_pipeline; }
    [[nodiscard]] auto Layout() const -> VkPipelineLayout { return m_layout; }
    [[nodiscard]] auto Format() const -> VkFormat { return m_format; }

    auto CreateTextureSet(uint32_t set, std::shared_ptr<Image> texture, VkFilter filter) -> Result<std::shared_ptr<DescriptorSet>>;
    auto CreateUniformSet(uint32_t set, std::span<const float> data) -> Result<std::shared_ptr<DescriptorSet>>;

    auto CreatePass(
        VkExtent2D viewport,
        std::shared_ptr<Buffer> vertices,
        std::vector<std::shared_ptr<DescriptorSet>> descriptor_sets
    ) -> Pass;
    auto CreatePassForTarget(const Image& target, std::vector<std::shared_ptr<DescriptorSet>> descriptor_sets) -> Pass;

    Pipeline(const Pipeline&) = delete;
    auto operator=(const Pipeline&) -> Pipeline& = delete;

  private:
    auto AllocateSet(uint32_t set) -> Result<std::shared_ptr<DescriptorSet>>;

    VkPipeline m_pipeline;
    VkPipelineLayout m_layout;
    std::vector<VkDescriptorSetLayout> m_set_layouts;
    VkFormat m_format;
};
