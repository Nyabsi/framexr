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

#include "Queue.hpp"

#include <format>

Queue::Queue(VkQueue queue, uint32_t family) : queue_(queue), family_(family) {
}

auto Queue::Submit(std::span<const VkCommandBuffer> command_buffers, VkFence fence) -> Result<> {
    const VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = static_cast<uint32_t>(command_buffers.size()),
        .pCommandBuffers = command_buffers.data(),
    };
    std::scoped_lock lock(mutex_);
    if (const auto result = vkQueueSubmit(queue_, 1, &submit_info, fence); result != VK_SUCCESS) {
        return Unexpected(std::format("vkQueueSubmit failed: {}", static_cast<int>(result)));
    }
    return {};
}

auto Queue::WaitIdle() -> void {
    std::scoped_lock lock(mutex_);
    vkQueueWaitIdle(queue_);
}
