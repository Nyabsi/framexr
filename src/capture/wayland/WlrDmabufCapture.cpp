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

#include "WlrDmabufCapture.hpp"

#include <chrono>
#include <utility>

#include <unistd.h>

#include "Error.hpp"
#include "Logger.hpp"
#include "wlr-export-dmabuf-unstable-v1-client-protocol.h"

WlrDmabufCapture::WlrDmabufCapture(std::unique_ptr<WaylandClient> client, uint32_t output_id)
    : m_output_id(output_id),
      m_client(std::move(client)) {
}

WlrDmabufCapture::~WlrDmabufCapture() {
    if (m_worker.valid()) {
        m_worker.wait();
    }
    if (m_channel) {
        for (const auto& frame : m_channel->TakeAll()) {
            CloseFrame(frame);
        }
    }
    for (const int fd : m_fds) {
        close(fd);
    }
}

auto WlrDmabufCapture::Start(std::span<const DrmFormat>, FrameProcessor processor) -> void {
    m_channel = std::make_shared<FrameChannel<DmabufFrame>>(2);
    m_processor = std::move(processor);
}

auto WlrDmabufCapture::CloseFrame(const DmabufFrame& frame) -> void {
    for (const auto& plane : frame.planes) {
        if (plane.fd >= 0) {
            close(plane.fd);
        }
    }
}

auto WlrDmabufCapture::Receive() -> std::unique_ptr<ProcessedFrame> {
    if (!m_channel) {
        return nullptr;
    }
    auto frames = m_channel->TakeAll();
    if (frames.empty()) {
        return nullptr;
    }
    auto latest = std::move(frames.back());
    frames.pop_back();
    for (const auto& frame : frames) {
        CloseFrame(frame);
    }

    while (m_fds.size() > 6 * latest.planes.size()) {
        close(m_fds.back());
        m_fds.pop_back();
    }
    for (const auto& plane : latest.planes) {
        if (plane.fd >= 0) {
            m_fds.push_front(plane.fd);
        }
    }
    return m_processor(Frame(std::move(latest)));
}

auto WlrDmabufCapture::Resume() -> void {
    Receive();
}

auto WlrDmabufCapture::RequestNewFrame() -> void {
    if (m_worker.valid()) {
        if (m_worker.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }
        m_client = m_worker.get();
    }
    if (!m_client) {
        return;
    }
    if (!m_channel) {
        FatalError("must call Start once before RequestNewFrame");
    }
    m_worker = std::async(std::launch::async, &WlrDmabufCapture::CaptureFrame, std::move(m_client), m_output_id, m_channel);
}

auto WlrDmabufCapture::CaptureFrame(
    std::unique_ptr<WaylandClient> client,
    uint32_t output_id,
    const std::shared_ptr<FrameChannel<DmabufFrame>>& channel
) -> std::unique_ptr<WaylandClient> {
    static constexpr zwlr_export_dmabuf_frame_v1_listener kListener{
        .frame = &WlrDmabufCapture::OnFrame,
        .object = &WlrDmabufCapture::OnObject,
        .ready = &WlrDmabufCapture::OnReady,
        .cancel = &WlrDmabufCapture::OnCancel,
    };

    auto* manager = client->DmabufManager();
    const auto* output = client->FindOutput(output_id);
    if (manager == nullptr || output == nullptr) {
        return client;
    }

    DmabufFrameRequest request{.transform = WaylandClient::ToFrameTransform(output->transform)};
    auto* frame = zwlr_export_dmabuf_manager_v1_capture_output(manager, 1, output->output);
    zwlr_export_dmabuf_frame_v1_add_listener(frame, &kListener, &request);

    while (!request.finished) {
        if (wl_display_dispatch(client->Display()) < 0) {
            zwlr_export_dmabuf_frame_v1_destroy(frame);
            CloseFrame(request.frame);
            return client;
        }
    }

    if (request.ready) {
        LOG_DEBUG("DMA-Buf frame captured");
        if (const auto evicted = channel->Push(std::move(request.frame))) {
            CloseFrame(*evicted);
        }
    } else {
        CloseFrame(request.frame);
    }
    return client;
}

auto WlrDmabufCapture::OnFrame(
    void* data,
    zwlr_export_dmabuf_frame_v1*,
    uint32_t width,
    uint32_t height,
    uint32_t,
    uint32_t,
    uint32_t,
    uint32_t,
    uint32_t format,
    uint32_t mod_high,
    uint32_t mod_low,
    uint32_t num_objects
) -> void {
    auto* request = static_cast<DmabufFrameRequest*>(data);
    request->started = true;
    request->frame.format = {
        .width = width,
        .height = height,
        .fourcc = format,
        .modifier = (static_cast<uint64_t>(mod_high) << 32) + mod_low,
        .transform = request->transform,
    };
    request->frame.planes.assign(num_objects, FramePlane{});
}

auto WlrDmabufCapture::OnObject(
    void* data,
    zwlr_export_dmabuf_frame_v1*,
    uint32_t index,
    int32_t fd,
    uint32_t,
    uint32_t offset,
    uint32_t stride,
    uint32_t
) -> void {
    auto* request = static_cast<DmabufFrameRequest*>(data);
    if (!request->started || index >= request->frame.planes.size()) {
        close(fd);
        return;
    }
    request->frame.planes[index] = {.fd = fd, .offset = offset, .stride = static_cast<int32_t>(stride)};
}

auto WlrDmabufCapture::OnReady(void* data, zwlr_export_dmabuf_frame_v1* frame, uint32_t, uint32_t, uint32_t) -> void {
    auto* request = static_cast<DmabufFrameRequest*>(data);
    zwlr_export_dmabuf_frame_v1_destroy(frame);
    request->ready = request->started;
    request->finished = true;
}

auto WlrDmabufCapture::OnCancel(void* data, zwlr_export_dmabuf_frame_v1* frame, uint32_t) -> void {
    auto* request = static_cast<DmabufFrameRequest*>(data);
    LOG_WARN("DMA-Buf frame capture cancelled");
    zwlr_export_dmabuf_frame_v1_destroy(frame);
    request->finished = true;
}
