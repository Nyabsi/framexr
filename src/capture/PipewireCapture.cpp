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

#include "PipewireCapture.hpp"

#include <array>
#include <cstddef>
#include <mutex>
#include <string_view>
#include <utility>

#include <spa/buffer/meta.h>
#include <spa/param/buffers.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <spa/utils/result.h>

#include "Error.hpp"
#include "Logger.hpp"

namespace {

auto EnsurePipewireInitialized() -> void {
    static std::once_flag once;
    std::call_once(once, []() -> void { pw_init(nullptr, nullptr); });
}

auto FourccToSpaFormat(uint32_t fourcc) -> spa_video_format {
    switch (fourcc) {
    case kDrmFormatArgb8888: return SPA_VIDEO_FORMAT_BGRA;
    case kDrmFormatAbgr8888: return SPA_VIDEO_FORMAT_RGBA;
    case kDrmFormatXrgb8888: return SPA_VIDEO_FORMAT_BGRx;
    case kDrmFormatXbgr8888: return SPA_VIDEO_FORMAT_RGBx;
    case kDrmFormatAbgr2101010: return SPA_VIDEO_FORMAT_ABGR_210LE;
    case kDrmFormatXbgr2101010: return SPA_VIDEO_FORMAT_xBGR_210LE;
    default: FatalError("Unsupported format");
    }
}

auto SpaFormatToFourcc(spa_video_format format) -> uint32_t {
    switch (format) {
    case SPA_VIDEO_FORMAT_BGRA: return kDrmFormatArgb8888;
    case SPA_VIDEO_FORMAT_RGBA: return kDrmFormatAbgr8888;
    case SPA_VIDEO_FORMAT_BGRx: return kDrmFormatXrgb8888;
    case SPA_VIDEO_FORMAT_RGBx: return kDrmFormatXbgr8888;
    case SPA_VIDEO_FORMAT_ABGR_210LE: return kDrmFormatAbgr2101010;
    case SPA_VIDEO_FORMAT_xBGR_210LE: return kDrmFormatXbgr2101010;
    default: FatalError("Unsupported format");
    }
}

auto SpaFormatName(spa_video_format format) -> std::string_view {
    switch (format) {
    case SPA_VIDEO_FORMAT_BGRA: return "VideoFormat::BGRA";
    case SPA_VIDEO_FORMAT_RGBA: return "VideoFormat::RGBA";
    case SPA_VIDEO_FORMAT_BGRx: return "VideoFormat::BGRx";
    case SPA_VIDEO_FORMAT_RGBx: return "VideoFormat::RGBx";
    case SPA_VIDEO_FORMAT_ABGR_210LE: return "VideoFormat::ABGR_210LE";
    case SPA_VIDEO_FORMAT_xBGR_210LE: return "VideoFormat::xBGR_210LE";
    default: return "VideoFormat::Unknown";
    }
}

auto StreamStateName(pw_stream_state state) -> std::string_view {
    switch (state) {
    case PW_STREAM_STATE_ERROR: return "Error";
    case PW_STREAM_STATE_UNCONNECTED: return "Unconnected";
    case PW_STREAM_STATE_CONNECTING: return "Connecting";
    case PW_STREAM_STATE_PAUSED: return "Paused";
    case PW_STREAM_STATE_STREAMING: return "Streaming";
    }
    return "Unknown";
}

auto TransformName(FrameTransform transform) -> std::string_view {
    switch (transform) {
    case FrameTransform::Undefined: return "Undefined";
    case FrameTransform::Normal: return "Normal";
    case FrameTransform::Rotated90: return "Rotated90";
    case FrameTransform::Rotated180: return "Rotated180";
    case FrameTransform::Rotated270: return "Rotated270";
    case FrameTransform::Flipped: return "Flipped";
    case FrameTransform::Flipped90: return "Flipped90";
    case FrameTransform::Flipped180: return "Flipped180";
    case FrameTransform::Flipped270: return "Flipped270";
    }
    return "Undefined";
}

auto TransformFromMeta(uint32_t transform) -> FrameTransform {
    switch (transform) {
    case SPA_META_TRANSFORMATION_None: return FrameTransform::Normal;
    case SPA_META_TRANSFORMATION_90: return FrameTransform::Rotated90;
    case SPA_META_TRANSFORMATION_180: return FrameTransform::Rotated180;
    case SPA_META_TRANSFORMATION_270: return FrameTransform::Rotated270;
    case SPA_META_TRANSFORMATION_Flipped: return FrameTransform::Flipped;
    case SPA_META_TRANSFORMATION_Flipped90: return FrameTransform::Flipped90;
    case SPA_META_TRANSFORMATION_Flipped180: return FrameTransform::Flipped180;
    case SPA_META_TRANSFORMATION_Flipped270: return FrameTransform::Flipped270;
    default: return FrameTransform::Undefined;
    }
}

auto BuildFormatParam(std::vector<std::byte>& storage, const DrmFormat* drm_format) -> const spa_pod* {
    spa_pod_builder builder{};
    spa_pod_builder_init(&builder, storage.data(), static_cast<uint32_t>(storage.size()));

    spa_pod_frame object_frame{};
    spa_pod_builder_push_object(&builder, &object_frame, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_prop(&builder, SPA_FORMAT_mediaType, 0);
    spa_pod_builder_id(&builder, SPA_MEDIA_TYPE_video);
    spa_pod_builder_prop(&builder, SPA_FORMAT_mediaSubtype, 0);
    spa_pod_builder_id(&builder, SPA_MEDIA_SUBTYPE_raw);

    spa_pod_frame choice_frame{};
    spa_pod_builder_prop(&builder, SPA_FORMAT_VIDEO_size, 0);
    spa_pod_builder_push_choice(&builder, &choice_frame, SPA_CHOICE_Range, 0);
    spa_pod_builder_rectangle(&builder, 256, 256);
    spa_pod_builder_rectangle(&builder, 1, 1);
    spa_pod_builder_rectangle(&builder, 8192, 8192);
    spa_pod_builder_pop(&builder, &choice_frame);

    spa_pod_builder_prop(&builder, SPA_FORMAT_VIDEO_framerate, 0);
    spa_pod_builder_push_choice(&builder, &choice_frame, SPA_CHOICE_Range, 0);
    spa_pod_builder_fraction(&builder, 0, 1);
    spa_pod_builder_fraction(&builder, 0, 1);
    spa_pod_builder_fraction(&builder, 1000, 1);
    spa_pod_builder_pop(&builder, &choice_frame);

    spa_pod_builder_prop(&builder, SPA_FORMAT_VIDEO_format, 0);
    spa_pod_builder_push_choice(&builder, &choice_frame, SPA_CHOICE_Enum, 0);
    if (drm_format != nullptr) {
        const auto format = FourccToSpaFormat(drm_format->fourcc);
        spa_pod_builder_id(&builder, format);
        spa_pod_builder_id(&builder, format);
    } else {
        constexpr std::array kFallbackFormats{
            SPA_VIDEO_FORMAT_RGBA,
            SPA_VIDEO_FORMAT_RGBA,
            SPA_VIDEO_FORMAT_BGRA,
            SPA_VIDEO_FORMAT_RGBx,
            SPA_VIDEO_FORMAT_BGRx,
            SPA_VIDEO_FORMAT_ABGR_210LE,
            SPA_VIDEO_FORMAT_xBGR_210LE,
        };
        for (const auto format : kFallbackFormats) {
            spa_pod_builder_id(&builder, format);
        }
    }
    spa_pod_builder_pop(&builder, &choice_frame);

    if (drm_format != nullptr) {
        spa_pod_builder_prop(&builder, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY | SPA_POD_PROP_FLAG_DONT_FIXATE);
        spa_pod_builder_push_choice(&builder, &choice_frame, SPA_CHOICE_Enum, 0);
        spa_pod_builder_long(&builder, static_cast<int64_t>(drm_format->modifiers.front()));
        for (const auto modifier : drm_format->modifiers) {
            spa_pod_builder_long(&builder, static_cast<int64_t>(modifier));
        }
        spa_pod_builder_pop(&builder, &choice_frame);
    }

    return static_cast<const spa_pod*>(spa_pod_builder_pop(&builder, &object_frame));
}

auto BuildBuffersParam(std::vector<std::byte>& storage) -> const spa_pod* {
    spa_pod_builder builder{};
    spa_pod_builder_init(&builder, storage.data(), static_cast<uint32_t>(storage.size()));
    spa_pod_frame frame{};
    spa_pod_builder_push_object(&builder, &frame, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers);
    spa_pod_builder_prop(&builder, SPA_PARAM_BUFFERS_dataType, 0);
    spa_pod_builder_int(&builder, (1 << SPA_DATA_MemFd) | (1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_DmaBuf));
    return static_cast<const spa_pod*>(spa_pod_builder_pop(&builder, &frame));
}

auto BuildMetaParam(std::vector<std::byte>& storage, uint32_t type, size_t size) -> const spa_pod* {
    spa_pod_builder builder{};
    spa_pod_builder_init(&builder, storage.data(), static_cast<uint32_t>(storage.size()));
    spa_pod_frame frame{};
    spa_pod_builder_push_object(&builder, &frame, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta);
    spa_pod_builder_prop(&builder, SPA_PARAM_META_type, 0);
    spa_pod_builder_id(&builder, type);
    spa_pod_builder_prop(&builder, SPA_PARAM_META_size, 0);
    spa_pod_builder_int(&builder, static_cast<int32_t>(size));
    return static_cast<const spa_pod*>(spa_pod_builder_pop(&builder, &frame));
}

} // namespace

PipewireCapture::PipewireCapture(std::string name, uint32_t node_id) : name_(std::move(name)), node_id_(node_id) {
}

PipewireCapture::~PipewireCapture() {
    if (thread_.joinable()) {
        SendCommand(PipewireCommand::Stop);
        thread_.join();
    }
}

auto PipewireCapture::Start(std::span<const DrmFormat> dmabuf_formats, FrameProcessor processor) -> void {
    channel_ = std::make_shared<FrameChannel<std::unique_ptr<ProcessedFrame>>>(2);
    processor_ = std::move(processor);
    thread_ = std::thread(&PipewireCapture::Run, this, std::vector<DrmFormat>(dmabuf_formats.begin(), dmabuf_formats.end()));
}

auto PipewireCapture::Receive() -> std::unique_ptr<ProcessedFrame> {
    if (!channel_) {
        return nullptr;
    }
    auto frames = channel_->TakeAll();
    if (frames.empty()) {
        return nullptr;
    }
    return std::move(frames.back());
}

auto PipewireCapture::Pause() -> void {
    if (channel_) {
        SendCommand(PipewireCommand::Pause);
    }
}

auto PipewireCapture::Resume() -> void {
    if (channel_) {
        SendCommand(PipewireCommand::Resume);
        LOG_DEBUG("{}: dropped {} old frames before resuming", name_, channel_->TakeAll().size());
    }
}

auto PipewireCapture::SendCommand(PipewireCommand command) -> void {
    std::scoped_lock lock(command_mutex_);
    commands_.push_back(command);
    if (loop_ != nullptr && command_event_ != nullptr) {
        pw_loop_signal_event(pw_main_loop_get_loop(loop_), command_event_);
    }
}

auto PipewireCapture::ExecuteCommands() -> void {
    std::deque<PipewireCommand> commands;
    {
        std::scoped_lock lock(command_mutex_);
        commands = std::exchange(commands_, {});
    }
    for (const auto command : commands) {
        switch (command) {
        case PipewireCommand::Pause: pw_stream_set_active(stream_, false); break;
        case PipewireCommand::Resume: pw_stream_set_active(stream_, true); break;
        case PipewireCommand::Stop:
            pw_main_loop_quit(loop_);
            LOG_INFO("{}: stopping pipewire loop", name_);
            break;
        }
    }
}

auto PipewireCapture::Run(const std::vector<DrmFormat>& dmabuf_formats) -> void {
    static constexpr pw_stream_events kStreamEvents{
        .version = PW_VERSION_STREAM_EVENTS,
        .state_changed = &PipewireCapture::OnStateChanged,
        .param_changed = &PipewireCapture::OnParamChanged,
        .process = &PipewireCapture::OnProcess,
    };

    EnsurePipewireInitialized();

    auto* loop = pw_main_loop_new(nullptr);
    if (loop == nullptr) {
        LOG_ERROR("{}: failed to create pipewire main loop", name_);
        return;
    }
    auto* context = pw_context_new(pw_main_loop_get_loop(loop), nullptr, 0);
    auto* core = context != nullptr ? pw_context_connect(context, nullptr, 0) : nullptr;
    if (core == nullptr) {
        LOG_ERROR("{}: failed to connect to pipewire", name_);
        if (context != nullptr) {
            pw_context_destroy(context);
        }
        pw_main_loop_destroy(loop);
        return;
    }

    stream_ = pw_stream_new(
        core,
        name_.c_str(),
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr)
    );
    pw_stream_add_listener(stream_, &stream_listener_, &kStreamEvents, this);

    std::vector<std::vector<std::byte>> storage;
    std::vector<const spa_pod*> params;
    for (const auto& format : dmabuf_formats) {
        storage.emplace_back(1024 + format.modifiers.size() * 16);
        params.push_back(BuildFormatParam(storage.back(), &format));
    }
    storage.emplace_back(1024);
    params.push_back(BuildFormatParam(storage.back(), nullptr));

    pw_stream_connect(
        stream_,
        PW_DIRECTION_INPUT,
        node_id_,
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
        params.data(),
        static_cast<uint32_t>(params.size())
    );

    {
        std::scoped_lock lock(command_mutex_);
        loop_ = loop;
        command_event_ = pw_loop_add_event(pw_main_loop_get_loop(loop), &PipewireCapture::OnCommand, this);
        if (!commands_.empty()) {
            pw_loop_signal_event(pw_main_loop_get_loop(loop), command_event_);
        }
    }

    pw_main_loop_run(loop);
    LOG_INFO("{}: pipewire loop exited", name_);

    {
        std::scoped_lock lock(command_mutex_);
        loop_ = nullptr;
        command_event_ = nullptr;
    }
    pw_stream_destroy(stream_);
    stream_ = nullptr;
    pw_core_disconnect(core);
    pw_context_destroy(context);
    pw_main_loop_destroy(loop);
}

auto PipewireCapture::OnCommand(void* data, uint64_t) -> void {
    static_cast<PipewireCapture*>(data)->ExecuteCommands();
}

auto PipewireCapture::OnStateChanged(void* data, pw_stream_state old_state, pw_stream_state state, const char* error) -> void {
    const auto* self = static_cast<PipewireCapture*>(data);
    if (state == PW_STREAM_STATE_ERROR && error != nullptr) {
        LOG_INFO("{}: stream state changed: {} -> Error(\"{}\")", self->name_, StreamStateName(old_state), error);
        return;
    }
    LOG_INFO("{}: stream state changed: {} -> {}", self->name_, StreamStateName(old_state), StreamStateName(state));
}

auto PipewireCapture::OnParamChanged(void* data, uint32_t id, const spa_pod* param) -> void {
    auto* self = static_cast<PipewireCapture*>(data);
    if (param == nullptr || id != SPA_PARAM_Format) {
        return;
    }

    spa_video_info_raw info{};
    if (spa_format_video_raw_parse(param, &info) < 0) {
        LOG_ERROR("{}: Failed to parse param changed to VideoInfoRaw", self->name_);
        return;
    }

    self->format_.width = info.size.width;
    self->format_.height = info.size.height;
    self->format_.fourcc = SpaFormatToFourcc(info.format);
    self->format_.modifier = info.modifier;

    const auto* kind = self->format_.modifier != 0 ? "DMA-buf" : "SHM";
    LOG_INFO("{}: got {} video format:", self->name_, kind);
    LOG_INFO("  format: {} ({})", static_cast<uint32_t>(info.format), SpaFormatName(info.format));
    LOG_INFO("  size: {}x{}", info.size.width, info.size.height);
    LOG_INFO("  modifier: {}", info.modifier);

    self->UpdateStreamParams();
}

auto PipewireCapture::UpdateStreamParams() -> void {
    std::vector<std::byte> buffers_storage(256);
    std::vector<std::byte> header_storage(256);
    std::vector<std::byte> transform_storage(256);
    std::array params{
        BuildBuffersParam(buffers_storage),
        BuildMetaParam(header_storage, SPA_META_Header, sizeof(spa_meta_header)),
        BuildMetaParam(transform_storage, SPA_META_VideoTransform, sizeof(spa_meta_videotransform)),
    };
    if (const int result = pw_stream_update_params(stream_, params.data(), static_cast<uint32_t>(params.size())); result < 0) {
        LOG_ERROR("{}: failed to update params: {}", name_, spa_strerror(result));
    }
}

auto PipewireCapture::OnProcess(void* data) -> void {
    auto* self = static_cast<PipewireCapture*>(data);
    pw_buffer* newest = nullptr;
    while (auto* buffer = pw_stream_dequeue_buffer(self->stream_)) {
        if (newest != nullptr) {
            pw_stream_queue_buffer(self->stream_, newest);
        }
        newest = buffer;
    }
    if (newest == nullptr) {
        return;
    }
    self->HandleBuffer(*newest->buffer);
    pw_stream_queue_buffer(self->stream_, newest);
}

auto PipewireCapture::HandleBuffer(const spa_buffer& buffer) -> void {
    if (const auto* header = static_cast<const spa_meta_header*>(
            spa_buffer_find_meta_data(&buffer, SPA_META_Header, sizeof(spa_meta_header))
        );
        header != nullptr && (header->flags & SPA_META_HEADER_FLAG_CORRUPTED) != 0) {
        LOG_WARN("{}: PipeWire buffer is corrupt.", name_);
        return;
    }

    if (const auto* transform = static_cast<const spa_meta_videotransform*>(
            spa_buffer_find_meta_data(&buffer, SPA_META_VideoTransform, sizeof(spa_meta_videotransform))
        )) {
        format_.transform = TransformFromMeta(transform->transform);
        LOG_DEBUG("{}: Transform: {}", name_, TransformName(format_.transform));
    }

    std::optional<MouseMeta> mouse;
    if (const auto* cursor = static_cast<const spa_meta_cursor*>(
            spa_buffer_find_meta_data(&buffer, SPA_META_Cursor, sizeof(spa_meta_cursor))
        );
        cursor != nullptr && cursor->id != 0) {
        mouse = MouseMeta{
            .x = static_cast<float>(cursor->position.x) / static_cast<float>(format_.width),
            .y = static_cast<float>(cursor->position.y) / static_cast<float>(format_.height),
        };
    }

    if (buffer.n_datas == 0) {
        LOG_DEBUG("{}: no data", name_);
        return;
    }

    const std::span datas(buffer.datas, buffer.n_datas);
    std::vector<FramePlane> planes;
    planes.reserve(datas.size());
    for (const auto& plane_data : datas) {
        planes.push_back({
            .fd = static_cast<int>(plane_data.fd),
            .offset = plane_data.chunk->offset,
            .stride = plane_data.chunk->stride,
        });
    }

    std::optional<Frame> frame;
    switch (datas.front().type) {
    case SPA_DATA_DmaBuf: frame = DmabufFrame{.format = format_, .planes = std::move(planes), .mouse = mouse}; break;
    case SPA_DATA_MemFd: frame = MemFdFrame{.format = format_, .plane = planes.front(), .mouse = mouse}; break;
    case SPA_DATA_MemPtr:
        frame = MemPtrFrame{
            .format = format_,
            .data = static_cast<const std::byte*>(datas.front().data),
            .size = datas.front().chunk->size,
            .mouse = mouse,
        };
        break;
    default: LOG_ERROR("Received invalid frame data type ({})", datas.front().type); return;
    }

    if (auto result = processor_(std::move(*frame))) {
        channel_->Push(std::move(result));
    }
}
