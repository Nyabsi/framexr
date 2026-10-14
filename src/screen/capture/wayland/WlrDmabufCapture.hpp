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
#include <deque>
#include <future>
#include <memory>

#include "Capture.hpp"
#include "FrameChannel.hpp"
#include "WaylandClient.hpp"

struct zwlr_export_dmabuf_frame_v1;

struct DmabufFrameRequest {
    DmabufFrame frame;
    FrameTransform transform = FrameTransform::Undefined;
    bool started = false;
    bool ready = false;
    bool finished = false;
};

class WlrDmabufCapture final : public Capture {
  public:
    WlrDmabufCapture(std::unique_ptr<WaylandClient> client, uint32_t output_id);
    ~WlrDmabufCapture() override;

    auto Start(std::span<const DrmFormat> dmabuf_formats, FrameProcessor processor) -> void override;
    [[nodiscard]] auto IsStarted() const -> bool override { return m_channel != nullptr; }
    [[nodiscard]] auto SupportsDmabuf() const -> bool override { return true; }
    auto Receive() -> std::unique_ptr<ProcessedFrame> override;
    auto Pause() -> void override {}
    auto Resume() -> void override;
    auto RequestNewFrame() -> void override;

    WlrDmabufCapture(const WlrDmabufCapture&) = delete;
    auto operator=(const WlrDmabufCapture&) -> WlrDmabufCapture& = delete;

  private:
    static auto CaptureFrame(
        std::unique_ptr<WaylandClient> client,
        uint32_t output_id,
        const std::shared_ptr<FrameChannel<DmabufFrame>>& channel
    ) -> std::unique_ptr<WaylandClient>;
    static auto CloseFrame(const DmabufFrame& frame) -> void;

    static auto OnFrame(
        void* data,
        zwlr_export_dmabuf_frame_v1* frame,
        uint32_t width,
        uint32_t height,
        uint32_t offset_x,
        uint32_t offset_y,
        uint32_t buffer_flags,
        uint32_t flags,
        uint32_t format,
        uint32_t mod_high,
        uint32_t mod_low,
        uint32_t num_objects
    ) -> void;
    static auto OnObject(
        void* data,
        zwlr_export_dmabuf_frame_v1* frame,
        uint32_t index,
        int32_t fd,
        uint32_t size,
        uint32_t offset,
        uint32_t stride,
        uint32_t plane_index
    ) -> void;
    static auto OnReady(void* data, zwlr_export_dmabuf_frame_v1* frame, uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec)
        -> void;
    static auto OnCancel(void* data, zwlr_export_dmabuf_frame_v1* frame, uint32_t reason) -> void;

    uint32_t m_output_id;
    std::unique_ptr<WaylandClient> m_client;
    std::future<std::unique_ptr<WaylandClient>> m_worker;
    std::shared_ptr<FrameChannel<DmabufFrame>> m_channel;
    FrameProcessor m_processor;
    std::deque<int> m_fds;
};
