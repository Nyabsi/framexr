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

#include "WaylandClient.hpp"

#include <poll.h>

#include <algorithm>
#include <climits>
#include <format>

#include <wayland-client.h>

#include "Error.hpp"
#include "Logger.hpp"
#include "wlr-export-dmabuf-unstable-v1-client-protocol.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

namespace {

auto FormatPair(const Eigen::Vector2i& value) -> std::string {
    return std::format("({}, {})", value.x(), value.y());
}

auto TransformName(int32_t transform) -> std::string_view {
    switch (transform) {
    case WL_OUTPUT_TRANSFORM_NORMAL: return "Normal";
    case WL_OUTPUT_TRANSFORM_90: return "_90";
    case WL_OUTPUT_TRANSFORM_180: return "_180";
    case WL_OUTPUT_TRANSFORM_270: return "_270";
    case WL_OUTPUT_TRANSFORM_FLIPPED: return "Flipped";
    case WL_OUTPUT_TRANSFORM_FLIPPED_90: return "Flipped90";
    case WL_OUTPUT_TRANSFORM_FLIPPED_180: return "Flipped180";
    case WL_OUTPUT_TRANSFORM_FLIPPED_270: return "Flipped270";
    default: return "Unknown";
    }
}

auto FindGlobal(const std::vector<WaylandGlobal>& globals, std::string_view interface) -> const WaylandGlobal* {
    const auto it = std::ranges::find(globals, interface, &WaylandGlobal::interface);
    return it == globals.end() ? nullptr : &*it;
}

template <typename T>
auto BindGlobal(
    wl_registry* registry,
    const WaylandGlobal* global,
    const wl_interface* interface,
    uint32_t min_version,
    uint32_t max_version
) -> T* {
    if (global == nullptr || global->version < min_version) {
        return nullptr;
    }
    return static_cast<T*>(wl_registry_bind(registry, global->name, interface, std::min(global->version, max_version)));
}

} // namespace

auto WaylandClient::Connect() -> std::unique_ptr<WaylandClient> {
    static constexpr wl_registry_listener kRegistryListener{
        .global = &WaylandClient::OnGlobal,
        .global_remove = &WaylandClient::OnGlobalRemove,
    };

    auto client = std::unique_ptr<WaylandClient>(new WaylandClient());
    client->display_ = wl_display_connect(nullptr);
    if (client->display_ == nullptr) {
        return nullptr;
    }

    client->registry_ = wl_display_get_registry(client->display_);
    wl_registry_add_listener(client->registry_, &kRegistryListener, client.get());
    if (wl_display_roundtrip(client->display_) < 0) {
        return nullptr;
    }
    client->BindGlobals();
    client->Dispatch();
    return client;
}

auto WaylandClient::BindGlobals() -> void {
    collecting_globals_ = false;

    xdg_output_manager_ = BindGlobal<zxdg_output_manager_v1>(
        registry_,
        FindGlobal(initial_globals_, zxdg_output_manager_v1_interface.name),
        &zxdg_output_manager_v1_interface,
        2,
        3
    );
    if (xdg_output_manager_ == nullptr) {
        FatalError(zxdg_output_manager_v1_interface.name);
    }
    seat_ = BindGlobal<wl_seat>(registry_, FindGlobal(initial_globals_, wl_seat_interface.name), &wl_seat_interface, 4, 9);
    if (seat_ == nullptr) {
        FatalError(wl_seat_interface.name);
    }
    shm_ = BindGlobal<wl_shm>(registry_, FindGlobal(initial_globals_, wl_shm_interface.name), &wl_shm_interface, 1, 1);
    if (shm_ == nullptr) {
        FatalError(wl_shm_interface.name);
    }
    dmabuf_manager_ = BindGlobal<zwlr_export_dmabuf_manager_v1>(
        registry_,
        FindGlobal(initial_globals_, zwlr_export_dmabuf_manager_v1_interface.name),
        &zwlr_export_dmabuf_manager_v1_interface,
        1,
        1
    );
    screencopy_manager_ = BindGlobal<zwlr_screencopy_manager_v1>(
        registry_,
        FindGlobal(initial_globals_, zwlr_screencopy_manager_v1_interface.name),
        &zwlr_screencopy_manager_v1_interface,
        2,
        2
    );

    for (const auto& global : initial_globals_) {
        if (global.interface == wl_output_interface.name) {
            AddOutput(global.name, global.version);
        }
    }
    initial_globals_.clear();
}

WaylandClient::~WaylandClient() {
    for (const auto& [xdg_output, id] : xdg_outputs_) {
        zxdg_output_v1_destroy(xdg_output);
    }
    for (const auto& output : outputs_) {
        wl_output_destroy(output.output);
    }
    if (screencopy_manager_ != nullptr) {
        zwlr_screencopy_manager_v1_destroy(screencopy_manager_);
    }
    if (dmabuf_manager_ != nullptr) {
        zwlr_export_dmabuf_manager_v1_destroy(dmabuf_manager_);
    }
    if (xdg_output_manager_ != nullptr) {
        zxdg_output_manager_v1_destroy(xdg_output_manager_);
    }
    if (seat_ != nullptr) {
        wl_seat_release(seat_);
    }
    if (shm_ != nullptr) {
        wl_shm_destroy(shm_);
    }
    if (registry_ != nullptr) {
        wl_registry_destroy(registry_);
    }
    if (display_ != nullptr) {
        wl_display_disconnect(display_);
    }
}

auto WaylandClient::ToFrameTransform(int32_t transform) -> FrameTransform {
    switch (transform) {
    case WL_OUTPUT_TRANSFORM_NORMAL: return FrameTransform::Normal;
    case WL_OUTPUT_TRANSFORM_90: return FrameTransform::Rotated90;
    case WL_OUTPUT_TRANSFORM_180: return FrameTransform::Rotated180;
    case WL_OUTPUT_TRANSFORM_270: return FrameTransform::Rotated270;
    case WL_OUTPUT_TRANSFORM_FLIPPED: return FrameTransform::Flipped;
    case WL_OUTPUT_TRANSFORM_FLIPPED_90: return FrameTransform::Flipped90;
    case WL_OUTPUT_TRANSFORM_FLIPPED_180: return FrameTransform::Flipped180;
    case WL_OUTPUT_TRANSFORM_FLIPPED_270: return FrameTransform::Flipped270;
    default: return FrameTransform::Undefined;
    }
}

auto WaylandClient::FindOutput(uint32_t id) const -> const WaylandOutput* {
    const auto it = std::ranges::find(outputs_, id, &WaylandOutput::id);
    return it == outputs_.end() ? nullptr : &*it;
}

auto WaylandClient::OutputById(uint32_t id) -> WaylandOutput* {
    const auto it = std::ranges::find(outputs_, id, &WaylandOutput::id);
    return it == outputs_.end() ? nullptr : &*it;
}

auto WaylandClient::OutputByProxy(const wl_output* output) -> WaylandOutput* {
    const auto it = std::ranges::find(outputs_, output, &WaylandOutput::output);
    return it == outputs_.end() ? nullptr : &*it;
}

auto WaylandClient::OutputByXdgProxy(const zxdg_output_v1* xdg_output) -> WaylandOutput* {
    const auto it = std::ranges::find_if(xdg_outputs_, [&](const auto& entry) -> bool { return entry.first == xdg_output; });
    return it == xdg_outputs_.end() ? nullptr : OutputById(it->second);
}

auto WaylandClient::DesktopOrigin() const -> Eigen::Vector2i {
    Eigen::Vector2i origin(INT_MAX, INT_MAX);
    for (const auto& output : outputs_) {
        origin = origin.cwiseMin(output.logical_position);
    }
    return origin;
}

auto WaylandClient::DesktopExtent() const -> Eigen::Vector2i {
    Eigen::Vector2i extent(0, 0);
    for (const auto& output : outputs_) {
        extent = extent.cwiseMax(output.logical_position + output.logical_size);
    }
    const Eigen::Vector2i origin = DesktopOrigin();
    return {
        static_cast<int32_t>(static_cast<int64_t>(extent.x()) - origin.x()),
        static_cast<int32_t>(static_cast<int64_t>(extent.y()) - origin.y()),
    };
}

auto WaylandClient::TakeEvents() -> std::vector<OutputChangeEvent> {
    std::scoped_lock lock(mutex_);
    return std::exchange(events_, {});
}

auto WaylandClient::Dispatch() -> void {
    std::scoped_lock lock(mutex_);
    wl_display_dispatch(display_);
}

auto WaylandClient::DispatchPending() -> void {
    std::scoped_lock lock(mutex_);
    while (wl_display_prepare_read(display_) != 0) {
        wl_display_dispatch_pending(display_);
    }
    wl_display_flush(display_);

    pollfd descriptor{.fd = wl_display_get_fd(display_), .events = POLLIN, .revents = 0};
    if (poll(&descriptor, 1, 0) <= 0) {
        wl_display_cancel_read(display_);
        return;
    }
    if (wl_display_read_events(display_) < 0) {
        LOG_WARN("Error while reading from event queue: {}", wl_display_get_error(display_));
        return;
    }
    const auto dispatched = wl_display_dispatch_pending(display_);
    if (dispatched < 0) {
        LOG_WARN("Error while dispatching pending events: {}", wl_display_get_error(display_));
        return;
    }
    LOG_DEBUG("Dispatched {} pending events", dispatched);
}

auto WaylandClient::Roundtrip() -> void {
    auto* queue = wl_display_create_queue(display_);
    wl_display_roundtrip_queue(display_, queue);
    wl_event_queue_destroy(queue);
}

auto WaylandClient::AddOutput(uint32_t name, uint32_t version) -> void {
    static constexpr wl_output_listener kOutputListener{
        .geometry = &WaylandClient::OnOutputGeometry,
        .mode = &WaylandClient::OnOutputMode,
        .done = &WaylandClient::OnOutputDone,
        .scale = &WaylandClient::OnOutputScale,
        .name = &WaylandClient::OnOutputName,
        .description = &WaylandClient::OnOutputDescription,
    };
    static constexpr zxdg_output_v1_listener kXdgOutputListener{
        .logical_position = &WaylandClient::OnXdgLogicalPosition,
        .logical_size = &WaylandClient::OnXdgLogicalSize,
        .done = &WaylandClient::OnXdgDone,
        .name = &WaylandClient::OnXdgName,
        .description = &WaylandClient::OnXdgDescription,
    };

    const auto bind_version = std::min(version, static_cast<uint32_t>(wl_output_interface.version));
    auto* output = static_cast<wl_output*>(wl_registry_bind(registry_, name, &wl_output_interface, bind_version));
    wl_output_add_listener(output, &kOutputListener, this);
    auto* xdg_output = zxdg_output_manager_v1_get_xdg_output(xdg_output_manager_, output);
    zxdg_output_v1_add_listener(xdg_output, &kXdgOutputListener, this);

    outputs_.push_back({
        .output = output,
        .id = name,
        .name = "Unknown",
        .make = "Unknown",
        .model = "Unknown",
        .transform = WL_OUTPUT_TRANSFORM_NORMAL,
    });
    xdg_outputs_.emplace_back(xdg_output, name);
}

auto WaylandClient::FinalizeOutput(WaylandOutput& output) -> void {
    if (output.logical_size.x() < 0) {
        output.logical_position.x() += output.logical_size.x();
        output.logical_size.x() *= -1;
    }
    if (output.logical_size.y() < 0) {
        output.logical_position.y() += output.logical_size.y();
        output.logical_size.y() *= -1;
    }
    if (!output.done) {
        output.done = true;
        LOG_DEBUG(
            "Discovered WlOutput {}; Size: {}; Logical Size: {}; Pos: {}",
            output.name,
            FormatPair(output.size),
            FormatPair(output.logical_size),
            FormatPair(output.logical_position)
        );
    }
}

auto WaylandClient::HandleLogicalChange(WaylandOutput& output, bool finalize, std::string_view what, const Eigen::Vector2i& value)
    -> void {
    const bool was_done = output.done;
    if (finalize) {
        FinalizeOutput(output);
    }
    if (was_done) {
        LOG_INFO("{}: Logical {} changed to {}", output.name, what, FormatPair(value));
        events_.push_back({OutputChange::Logical, output.id});
    } else {
        events_.push_back({OutputChange::Create, output.id});
    }
}

auto WaylandClient::OnGlobal(void* data, wl_registry*, uint32_t name, const char* interface, uint32_t version) -> void {
    auto* self = static_cast<WaylandClient*>(data);
    if (self->collecting_globals_) {
        self->initial_globals_.push_back({.name = name, .interface = interface, .version = version});
        return;
    }
    if (std::string_view(interface) == wl_output_interface.name) {
        self->AddOutput(name, version);
        self->Roundtrip();
    }
}

auto WaylandClient::OnGlobalRemove(void* data, wl_registry*, uint32_t name) -> void {
    auto* self = static_cast<WaylandClient*>(data);
    const auto it = std::ranges::find(self->outputs_, name, &WaylandOutput::id);
    if (it == self->outputs_.end()) {
        return;
    }
    LOG_INFO("{}: Device removed", it->name);
    std::erase_if(self->xdg_outputs_, [&](const auto& entry) -> bool {
        if (entry.second != name) {
            return false;
        }
        zxdg_output_v1_destroy(entry.first);
        return true;
    });
    wl_output_destroy(it->output);
    self->outputs_.erase(it);
    self->events_.push_back({OutputChange::Destroy, name});
}

auto WaylandClient::OnOutputGeometry(
    void* data,
    wl_output* output,
    int32_t,
    int32_t,
    int32_t,
    int32_t,
    int32_t,
    const char* make,
    const char* model,
    int32_t transform
) -> void {
    auto* self = static_cast<WaylandClient*>(data);
    auto* entry = self->OutputByProxy(output);
    if (entry == nullptr) {
        return;
    }
    const auto old_transform = entry->transform;
    entry->transform = transform;
    if (entry->done && old_transform != transform) {
        LOG_INFO("{}: Transform changed {} -> {}", entry->name, TransformName(entry->transform), TransformName(transform));
        self->events_.push_back({OutputChange::Physical, entry->id});
        self->events_.push_back({OutputChange::Logical, entry->id});
    }
    entry->make = make;
    entry->model = model;
}

auto WaylandClient::OnOutputMode(void* data, wl_output* output, uint32_t, int32_t width, int32_t height, int32_t) -> void {
    auto* self = static_cast<WaylandClient*>(data);
    auto* entry = self->OutputByProxy(output);
    if (entry == nullptr) {
        return;
    }
    entry->size = {width, height};
    if (entry->done) {
        LOG_INFO("{}: Resolution changed {} -> {}", entry->name, FormatPair(entry->size), FormatPair(entry->size));
        self->events_.push_back({OutputChange::Physical, entry->id});
    }
}

auto WaylandClient::OnOutputDone(void*, wl_output*) -> void {
}

auto WaylandClient::OnOutputScale(void*, wl_output*, int32_t) -> void {
}

auto WaylandClient::OnOutputName(void*, wl_output*, const char*) -> void {
}

auto WaylandClient::OnOutputDescription(void*, wl_output*, const char*) -> void {
}

auto WaylandClient::OnXdgLogicalPosition(void* data, zxdg_output_v1* xdg_output, int32_t x, int32_t y) -> void {
    auto* self = static_cast<WaylandClient*>(data);
    auto* entry = self->OutputByXdgProxy(xdg_output);
    if (entry == nullptr) {
        return;
    }
    entry->logical_position = {x, y};
    self->HandleLogicalChange(*entry, entry->logical_size != Eigen::Vector2i::Zero(), "pos", entry->logical_position);
}

auto WaylandClient::OnXdgLogicalSize(void* data, zxdg_output_v1* xdg_output, int32_t width, int32_t height) -> void {
    auto* self = static_cast<WaylandClient*>(data);
    auto* entry = self->OutputByXdgProxy(xdg_output);
    if (entry == nullptr) {
        return;
    }
    entry->logical_size = {width, height};
    self->HandleLogicalChange(*entry, entry->logical_position != Eigen::Vector2i::Zero(), "size", entry->logical_size);
}

auto WaylandClient::OnXdgDone(void*, zxdg_output_v1*) -> void {
}

auto WaylandClient::OnXdgName(void* data, zxdg_output_v1* xdg_output, const char* name) -> void {
    auto* self = static_cast<WaylandClient*>(data);
    if (auto* entry = self->OutputByXdgProxy(xdg_output)) {
        entry->name = name;
    }
}

auto WaylandClient::OnXdgDescription(void*, zxdg_output_v1*, const char*) -> void {
}
