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
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Eigen/Core>

#include "Frame.hpp"

struct wl_display;
struct wl_registry;
struct wl_output;
struct wl_seat;
struct wl_shm;
struct zxdg_output_manager_v1;
struct zxdg_output_v1;
struct zwlr_export_dmabuf_manager_v1;
struct zwlr_screencopy_manager_v1;

enum class OutputChange : uint8_t {
    Create,
    Logical,
    Physical,
    Destroy,
};

struct OutputChangeEvent {
    OutputChange change;
    uint32_t id;
};

struct WaylandOutput {
    wl_output* output = nullptr;
    uint32_t id = 0;
    std::string name;
    std::string make;
    std::string model;
    Eigen::Vector2i size = Eigen::Vector2i::Zero();
    Eigen::Vector2i logical_position = Eigen::Vector2i::Zero();
    Eigen::Vector2i logical_size = Eigen::Vector2i::Zero();
    int32_t transform = 0;
    bool done = false;
};

struct WaylandGlobal {
    uint32_t name = 0;
    std::string interface;
    uint32_t version = 0;
};

class WaylandClient final {
  public:
    static auto Connect() -> std::unique_ptr<WaylandClient>;
    static auto ToFrameTransform(int32_t transform) -> FrameTransform;

    ~WaylandClient();

    [[nodiscard]] auto Display() const -> wl_display* { return display_; }
    [[nodiscard]] auto Shm() const -> wl_shm* { return shm_; }
    [[nodiscard]] auto DmabufManager() const -> zwlr_export_dmabuf_manager_v1* { return dmabuf_manager_; }
    [[nodiscard]] auto ScreencopyManager() const -> zwlr_screencopy_manager_v1* { return screencopy_manager_; }
    [[nodiscard]] auto Outputs() const -> const std::vector<WaylandOutput>& { return outputs_; }
    [[nodiscard]] auto FindOutput(uint32_t id) const -> const WaylandOutput*;
    [[nodiscard]] auto DesktopOrigin() const -> Eigen::Vector2i;
    [[nodiscard]] auto DesktopExtent() const -> Eigen::Vector2i;

    auto TakeEvents() -> std::vector<OutputChangeEvent>;
    auto Dispatch() -> void;
    auto DispatchPending() -> void;

    WaylandClient(const WaylandClient&) = delete;
    auto operator=(const WaylandClient&) -> WaylandClient& = delete;

  private:
    WaylandClient() = default;

    auto BindGlobals() -> void;
    auto AddOutput(uint32_t name, uint32_t version) -> void;
    auto Roundtrip() -> void;
    auto OutputById(uint32_t id) -> WaylandOutput*;
    auto OutputByProxy(const wl_output* output) -> WaylandOutput*;
    auto OutputByXdgProxy(const zxdg_output_v1* xdg_output) -> WaylandOutput*;
    auto HandleLogicalChange(WaylandOutput& output, bool finalize, std::string_view what, const Eigen::Vector2i& value) -> void;

    static auto FinalizeOutput(WaylandOutput& output) -> void;

    static auto OnGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) -> void;
    static auto OnGlobalRemove(void* data, wl_registry* registry, uint32_t name) -> void;
    static auto OnOutputGeometry(
        void* data,
        wl_output* output,
        int32_t x,
        int32_t y,
        int32_t physical_width,
        int32_t physical_height,
        int32_t subpixel,
        const char* make,
        const char* model,
        int32_t transform
    ) -> void;
    static auto OnOutputMode(void* data, wl_output* output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) -> void;
    static auto OnOutputDone(void* data, wl_output* output) -> void;
    static auto OnOutputScale(void* data, wl_output* output, int32_t factor) -> void;
    static auto OnOutputName(void* data, wl_output* output, const char* name) -> void;
    static auto OnOutputDescription(void* data, wl_output* output, const char* description) -> void;
    static auto OnXdgLogicalPosition(void* data, zxdg_output_v1* xdg_output, int32_t x, int32_t y) -> void;
    static auto OnXdgLogicalSize(void* data, zxdg_output_v1* xdg_output, int32_t width, int32_t height) -> void;
    static auto OnXdgDone(void* data, zxdg_output_v1* xdg_output) -> void;
    static auto OnXdgName(void* data, zxdg_output_v1* xdg_output, const char* name) -> void;
    static auto OnXdgDescription(void* data, zxdg_output_v1* xdg_output, const char* description) -> void;

    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_seat* seat_ = nullptr;
    wl_shm* shm_ = nullptr;
    zxdg_output_manager_v1* xdg_output_manager_ = nullptr;
    zwlr_export_dmabuf_manager_v1* dmabuf_manager_ = nullptr;
    zwlr_screencopy_manager_v1* screencopy_manager_ = nullptr;
    std::vector<WaylandGlobal> initial_globals_;
    bool collecting_globals_ = true;
    std::vector<WaylandOutput> outputs_;
    std::vector<std::pair<zxdg_output_v1*, uint32_t>> xdg_outputs_;
    std::vector<OutputChangeEvent> events_;
    std::mutex mutex_;
};
