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

#include "WlrScreencopyCapture.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <format>
#include <optional>
#include <utility>

#include <wayland-client.h>

#include "Error.hpp"
#include "Logger.hpp"
#include "wlr-screencopy-unstable-v1-client-protocol.h"

namespace {

auto FourccFromShmFormat(uint32_t format) -> std::optional<uint32_t> {
    switch (format) {
    case WL_SHM_FORMAT_ARGB8888:
    case WL_SHM_FORMAT_ABGR8888: return kDrmFormatArgb8888;
    case WL_SHM_FORMAT_XRGB8888:
    case WL_SHM_FORMAT_XBGR8888: return kDrmFormatXrgb8888;
    default: return std::nullopt;
    }
}

auto NextShmName() -> std::string {
    static std::atomic<uint32_t> counter{0};
    return std::format("framexr-{}", counter.fetch_add(1));
}

} // namespace

WlrScreencopyCapture::WlrScreencopyCapture(std::unique_ptr<WaylandClient> client, uint32_t output_id)
    : output_id_(output_id),
      client_(std::move(client)) {
}

WlrScreencopyCapture::~WlrScreencopyCapture() {
    if (worker_.valid()) {
        worker_.wait();
    }
}

auto WlrScreencopyCapture::Start(std::span<const DrmFormat>, FrameProcessor processor) -> void {
    channel_ = std::make_shared<ProcessedFrameChannel>(2);
    processor_ = std::move(processor);
}

auto WlrScreencopyCapture::Receive() -> std::unique_ptr<ProcessedFrame> {
    if (!channel_) {
        return nullptr;
    }
    auto frames = channel_->TakeAll();
    if (frames.empty()) {
        return nullptr;
    }
    return std::move(frames.back());
}

auto WlrScreencopyCapture::Resume() -> void {
    if (!channel_) {
        return;
    }
    Receive();
    RequestNewFrame();
}

auto WlrScreencopyCapture::RequestNewFrame() -> void {
    bool wait_for_damage = false;
    if (worker_.valid()) {
        if (worker_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }
        wait_for_damage = true;
        client_ = worker_.get();
    }
    if (!client_) {
        return;
    }
    if (!channel_) {
        FatalError("must call Start once before RequestNewFrame");
    }
    worker_ = std::async(
        std::launch::async,
        &WlrScreencopyCapture::CaptureFrame,
        std::move(client_),
        output_id_,
        channel_,
        processor_,
        wait_for_damage
    );
}

auto WlrScreencopyCapture::DestroyBuffer(ScreencopyBuffer& buffer) -> void {
    if (buffer.buffer != nullptr) {
        wl_buffer_destroy(buffer.buffer);
    }
    if (buffer.pool != nullptr) {
        wl_shm_pool_destroy(buffer.pool);
    }
    if (buffer.fd >= 0) {
        close(buffer.fd);
    }
    buffer = {};
}

auto WlrScreencopyCapture::CaptureFrame(
    std::unique_ptr<WaylandClient> client,
    uint32_t output_id,
    const std::shared_ptr<ProcessedFrameChannel>& channel,
    const FrameProcessor& processor,
    bool wait_for_damage
) -> std::unique_ptr<WaylandClient> {
    static constexpr zwlr_screencopy_frame_v1_listener kListener{
        .buffer = &WlrScreencopyCapture::OnBuffer,
        .flags = &WlrScreencopyCapture::OnFlags,
        .ready = &WlrScreencopyCapture::OnReady,
        .failed = &WlrScreencopyCapture::OnFailed,
        .damage = &WlrScreencopyCapture::OnDamage,
        .linux_dmabuf = &WlrScreencopyCapture::OnLinuxDmabuf,
        .buffer_done = &WlrScreencopyCapture::OnBufferDone,
    };

    auto* manager = client->ScreencopyManager();
    const auto* output = client->FindOutput(output_id);
    if (manager == nullptr || output == nullptr) {
        return client;
    }

    const std::string name = output->name;
    ScreencopyRequest request{
        .client = client.get(),
        .name = name,
        .transform = WaylandClient::ToFrameTransform(output->transform),
        .wait_for_damage = wait_for_damage,
    };
    request.frame = zwlr_screencopy_manager_v1_capture_output(manager, 1, output->output);
    zwlr_screencopy_frame_v1_add_listener(request.frame, &kListener, &request);

    while (!request.finished) {
        if (wl_display_dispatch(client->Display()) < 0) {
            zwlr_screencopy_frame_v1_destroy(request.frame);
            DestroyBuffer(request.buffer);
            return client;
        }
    }

    if (request.ready && request.has_buffer) {
        if (auto result = processor(Frame(request.mem_frame))) {
            channel->Push(std::move(result));
            LOG_TRACE("{}: Frame ready", name);
        }
    } else if (!request.ready) {
        LOG_TRACE("{}: Frame failed", name);
    }
    DestroyBuffer(request.buffer);
    return client;
}

auto WlrScreencopyCapture::OnBuffer(
    void* data,
    zwlr_screencopy_frame_v1* frame,
    uint32_t format,
    uint32_t width,
    uint32_t height,
    uint32_t stride
) -> void {
    auto* request = static_cast<ScreencopyRequest*>(data);
    if (request->has_buffer) {
        return;
    }

    const auto fourcc = FourccFromShmFormat(format);
    if (!fourcc) {
        LOG_WARN("Unsupported screencopy format");
        zwlr_screencopy_frame_v1_destroy(frame);
        request->finished = true;
        return;
    }

    const auto shm_name = NextShmName();
    const auto size = stride * height;
    const int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    shm_unlink(shm_name.c_str());
    if (fd < 0 || ftruncate(fd, size) != 0) {
        if (fd >= 0) {
            close(fd);
        }
        zwlr_screencopy_frame_v1_destroy(frame);
        request->finished = true;
        return;
    }

    request->buffer.fd = fd;
    request->buffer.pool = wl_shm_create_pool(request->client->Shm(), fd, static_cast<int32_t>(size));
    request->buffer.buffer = wl_shm_pool_create_buffer(
        request->buffer.pool,
        0,
        static_cast<int32_t>(width),
        static_cast<int32_t>(height),
        static_cast<int32_t>(stride),
        format
    );
    request->mem_frame = MemFdFrame{
        .format = {.width = width, .height = height, .fourcc = *fourcc, .transform = request->transform},
        .plane = {.fd = fd, .offset = 0, .stride = static_cast<int32_t>(stride)},
    };
    request->has_buffer = true;
    LOG_TRACE("{}: Received screencopy buffer, copying", request->name);

    if (request->wait_for_damage) {
        zwlr_screencopy_frame_v1_copy_with_damage(frame, request->buffer.buffer);
    } else {
        zwlr_screencopy_frame_v1_copy(frame, request->buffer.buffer);
    }
}

auto WlrScreencopyCapture::OnFlags(void*, zwlr_screencopy_frame_v1*, uint32_t) -> void {
}

auto WlrScreencopyCapture::OnReady(void* data, zwlr_screencopy_frame_v1* frame, uint32_t, uint32_t, uint32_t) -> void {
    auto* request = static_cast<ScreencopyRequest*>(data);
    zwlr_screencopy_frame_v1_destroy(frame);
    request->ready = true;
    request->finished = true;
}

auto WlrScreencopyCapture::OnFailed(void* data, zwlr_screencopy_frame_v1* frame) -> void {
    auto* request = static_cast<ScreencopyRequest*>(data);
    zwlr_screencopy_frame_v1_destroy(frame);
    request->finished = true;
}

auto WlrScreencopyCapture::OnDamage(void*, zwlr_screencopy_frame_v1*, uint32_t, uint32_t, uint32_t, uint32_t) -> void {
}

auto WlrScreencopyCapture::OnLinuxDmabuf(void*, zwlr_screencopy_frame_v1*, uint32_t, uint32_t, uint32_t) -> void {
}

auto WlrScreencopyCapture::OnBufferDone(void*, zwlr_screencopy_frame_v1*) -> void {
}
