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
#include "Image.hpp"

class DescriptorSet final
{
  public:
    DescriptorSet(VkDescriptorPool pool, VkDescriptorSet set);
    ~DescriptorSet();

    [[nodiscard]] auto Handle() const -> VkDescriptorSet { return m_set; }
    [[nodiscard]] auto Texture() const -> const std::shared_ptr<Image>& { return m_texture; }

    auto BindTexture(std::shared_ptr<Image> texture) -> void { m_texture = std::move(texture); }
    auto BindBuffer(std::shared_ptr<Buffer> buffer) -> void { m_buffer = std::move(buffer); }

    DescriptorSet(const DescriptorSet&) = delete;
    auto operator=(const DescriptorSet&) -> DescriptorSet& = delete;

  private:
    VkDescriptorPool m_pool;
    VkDescriptorSet m_set;
    std::shared_ptr<Image> m_texture;
    std::shared_ptr<Buffer> m_buffer;
};
