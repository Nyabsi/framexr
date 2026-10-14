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

#include "CommandBatch.hpp"

#include <utility>

#include "Device.hpp"

auto CommandBatch::Push(CommandBuffer command_buffer) -> void
{
    m_command_buffers.push_back(std::move(command_buffer));
}

auto CommandBatch::Submit() -> Result<>
{
    if (m_command_buffers.empty())
    {
        return {};
    }
    auto& queue = m_command_buffers.front().SubmitQueue();

    std::vector<VkCommandBuffer> handles;
    Submission submission;
    for (auto& command_buffer : m_command_buffers)
    {
        RETURN_IF_ERROR(command_buffer.Finish());
        handles.push_back(command_buffer.m_command_buffer);
        submission.pools.emplace_back(command_buffer.m_queue->Family(), std::exchange(command_buffer.m_pool, VK_NULL_HANDLE));
        for (auto& resource : command_buffer.m_retained)
        {
            submission.retained.push_back(std::move(resource));
        }
    }
    m_command_buffers.clear();
    return g_device->Submit(queue, handles, std::move(submission), false);
}
