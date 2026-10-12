// FrameXR
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
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
#include <mutex>
#include <span>

#include <vulkan/vulkan.h>

#include "Error.hpp"

class Queue final {
  public:
    Queue(VkQueue queue, uint32_t family);

    [[nodiscard]] auto Handle() const -> VkQueue { return m_queue; }
    [[nodiscard]] auto Family() const -> uint32_t { return m_family; }

    auto Submit(std::span<const VkCommandBuffer> command_buffers, VkFence fence) -> Result<>;
    auto WaitIdle() -> void;

    Queue(const Queue&) = delete;
    auto operator=(const Queue&) -> Queue& = delete;

  private:
    VkQueue m_queue;
    uint32_t m_family;
    std::mutex m_mutex;
};
