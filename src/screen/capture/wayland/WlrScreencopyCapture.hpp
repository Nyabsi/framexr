// FrameXR
//
// This is a port of wlx-capture from Rust to C++.
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
// Copyright (C) 2023-2025 galister and wlx-capture contributors
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
#include <future>
#include <memory>
#include <string>

#include "Capture.hpp"
#include "FrameChannel.hpp"
#include "WaylandClient.hpp"

struct wl_buffer;
struct wl_shm_pool;
struct zwlr_screencopy_frame_v1;

struct ScreencopyBuffer
{
    wl_shm_pool* pool = nullptr;
    wl_buffer* buffer = nullptr;
    int fd = -1;
};

struct ScreencopyRequest
{
    WaylandClient* client = nullptr;
    std::string name;
    zwlr_screencopy_frame_v1* frame = nullptr;
    FrameTransform transform = FrameTransform::Undefined;
    bool wait_for_damage = false;
    MemFdFrame mem_frame;
    ScreencopyBuffer buffer;
    bool has_buffer = false;
    bool ready = false;
    bool finished = false;
};

using ProcessedFrameChannel = FrameChannel<std::unique_ptr<ProcessedFrame>>;

class WlrScreencopyCapture final : public Capture
{
  public:
    WlrScreencopyCapture(std::unique_ptr<WaylandClient> client, uint32_t output_id);
    ~WlrScreencopyCapture() override;

    auto Start(std::span<const DrmFormat> dmabuf_formats, FrameProcessor processor) -> void override;
    [[nodiscard]] auto IsStarted() const -> bool override { return m_channel != nullptr; }
    [[nodiscard]] auto SupportsDmabuf() const -> bool override { return false; }
    auto Receive() -> std::unique_ptr<ProcessedFrame> override;
    auto Pause() -> void override {}
    auto Resume() -> void override;
    auto RequestNewFrame() -> void override;

    WlrScreencopyCapture(const WlrScreencopyCapture&) = delete;
    auto operator=(const WlrScreencopyCapture&) -> WlrScreencopyCapture& = delete;

  private:
    static auto CaptureFrame(
        std::unique_ptr<WaylandClient> client,
        uint32_t output_id,
        const std::shared_ptr<ProcessedFrameChannel>& channel,
        const FrameProcessor& processor,
        bool wait_for_damage
    ) -> std::unique_ptr<WaylandClient>;
    static auto DestroyBuffer(ScreencopyBuffer& buffer) -> void;

    static auto OnBuffer(
        void* data,
        zwlr_screencopy_frame_v1* frame,
        uint32_t format,
        uint32_t width,
        uint32_t height,
        uint32_t stride
    ) -> void;
    static auto OnFlags(void* data, zwlr_screencopy_frame_v1* frame, uint32_t flags) -> void;
    static auto OnReady(void* data, zwlr_screencopy_frame_v1* frame, uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec)
        -> void;
    static auto OnFailed(void* data, zwlr_screencopy_frame_v1* frame) -> void;
    static auto OnDamage(void* data, zwlr_screencopy_frame_v1* frame, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
        -> void;
    static auto OnLinuxDmabuf(void* data, zwlr_screencopy_frame_v1* frame, uint32_t format, uint32_t width, uint32_t height)
        -> void;
    static auto OnBufferDone(void* data, zwlr_screencopy_frame_v1* frame) -> void;

    uint32_t m_output_id;
    std::unique_ptr<WaylandClient> m_client;
    std::future<std::unique_ptr<WaylandClient>> m_worker;
    std::shared_ptr<ProcessedFrameChannel> m_channel;
    FrameProcessor m_processor;
};
