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

#include "FrameUploader.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>
#include <limits>
#include <numbers>
#include <ranges>
#include <utility>

#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "CommandBatch.hpp"
#include "CommandBuffer.hpp"
#include "Config.hpp"
#include "DbusNotificationSender.hpp"
#include "Device.hpp"
#include "HidProvider.hpp"
#include "Image.hpp"
#include "Interaction.hpp"
#include "Logger.hpp"
#include "Container.hpp"
#include "Compose.hpp"
#include "Pipeline.hpp"
#include "ScreenInteraction.hpp"
#include "ScreenRenderer.hpp"
#include "Shaders.hpp"
#include "Yaml.hpp"
#include "framexr_config.h"

#if FRAMEXR_FEATURE_WAYLAND
#include "Notifications.hpp"
#include "PipewireCapture.hpp"
#include "WaylandClient.hpp"
#include "WlrDmabufCapture.hpp"
#include "WlrScreencopyCapture.hpp"
#endif

#if FRAMEXR_FEATURE_X11
#include "XshmCapture.hpp"
#endif

#ifdef _WIN32
#include "DxgiCapture.hpp"
#endif

FrameUploader::FrameUploader(std::string name, Queue& queue) : m_name(std::move(name)), m_queue(&queue) {
}

auto FrameUploader::Upload(const FrameFormat& format, std::span<const std::byte> data, int32_t stride) const
    -> std::shared_ptr<Image> {
    const auto vk_format = Device::FourccToFormat(format.fourcc);
    if (!vk_format) {
        LOG_ERROR("{}: {}", m_name, vk_format.error().Message());
        return nullptr;
    }
    auto command_buffer = g_device->CreateCommandBuffer(*m_queue);
    if (!command_buffer) {
        LOG_ERROR("{}: Could not create vkCommandBuffer: {}", m_name, command_buffer.error().Message());
        return nullptr;
    }
    const uint32_t row_length = stride > 0 ? static_cast<uint32_t>(stride) / 4 : 0;
    auto image = command_buffer->UploadImage(format.width, format.height, *vk_format, data, row_length);
    if (!image) {
        LOG_ERROR("{}: Could not create vkImage: {}", m_name, image.error().Message());
        return nullptr;
    }
    if (const auto result = command_buffer->SubmitUpload(); !result) {
        LOG_ERROR("{}: Could not execute upload: {}", m_name, result.error().Message());
        return nullptr;
    }
    return std::move(*image);
}

auto FrameUploader::HasValidPlanes(const DmabufFrame& frame) -> bool {
    return std::ranges::all_of(frame.planes, [](const FramePlane& plane) -> bool { return plane.fd >= 0; });
}

auto FrameUploader::Process(Frame&& frame) const -> std::unique_ptr<CapturedFrame> {
    if (const auto* dmabuf = std::get_if<DmabufFrame>(&frame)) {
        if (!HasValidPlanes(*dmabuf)) {
            LOG_ERROR("{}: Invalid frame", m_name);
            return nullptr;
        }
        LOG_TRACE("{}: New DMA-buf frame", m_name);
        auto image = g_device->ImportDmabuf(*dmabuf);
        if (!image) {
            LOG_ERROR("{}: Failed to create DMA-buf vkImage: {}", m_name, image.error().Message());
            return nullptr;
        }
        return std::make_unique<CapturedFrame>(std::move(*image), dmabuf->format, std::nullopt);
    }

#ifdef _WIN32
    if (std::holds_alternative<MemFdFrame>(frame)) {
        LOG_ERROR("{}: MemFd frames are not supported on Windows", m_name);
        return nullptr;
    }
#else
    if (const auto* memfd = std::get_if<MemFdFrame>(&frame)) {
        if (memfd->plane.fd < 0) {
            LOG_ERROR("{}: No fd in MemFd frame", m_name);
            return nullptr;
        }
        const size_t length = static_cast<size_t>(memfd->plane.stride) * memfd->format.height;
        void* map = mmap(nullptr, length, PROT_READ, MAP_SHARED, memfd->plane.fd, static_cast<off_t>(memfd->plane.offset));
        if (map == MAP_FAILED) {
            LOG_ERROR("{}: Could not map MemFd frame", m_name);
            return nullptr;
        }
        auto image = Upload(memfd->format, std::span(static_cast<const std::byte*>(map), length), memfd->plane.stride);
        munmap(map, length);
        if (!image) {
            return nullptr;
        }
        return std::make_unique<CapturedFrame>(std::move(image), memfd->format, std::nullopt);
    }
#endif

    const auto& memptr = std::get<MemPtrFrame>(frame);
    LOG_TRACE("{}: New MemPtr frame", m_name);
    auto image = Upload(memptr.format, std::span(memptr.data, memptr.size), 0);
    if (!image) {
        return nullptr;
    }
    return std::make_unique<CapturedFrame>(std::move(image), memptr.format, memptr.mouse);
}
