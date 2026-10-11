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

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <openxr/openxr.h>

#include "Backend.hpp"
#include "Error.hpp"
#include "Monado.hpp"
#include "Notifications.hpp"
#include "OpenXrBlocker.hpp"
#include "OpenXrInput.hpp"
#include "OpenXrInstance.hpp"
#include "OpenXrLines.hpp"
#include "OpenXrOverlay.hpp"
#include "OpenXrPlayspace.hpp"
#include "OpenXrSkybox.hpp"
#include "OpenXrState.hpp"
#include "OverlayContainer.hpp"
#include "State.hpp"
#include "Task.hpp"

class OpenXrSession final {
  public:
    static auto Create(const OpenXrInstance& instance, Device& device) -> Result<std::unique_ptr<OpenXrSession>>;

    ~OpenXrSession();
    OpenXrSession(const OpenXrSession&) = delete;
    auto operator=(const OpenXrSession&) -> OpenXrSession& = delete;

    [[nodiscard]] auto Handle() const -> XrSession { return session_; }
    [[nodiscard]] auto Stage() const -> XrSpace { return stage_; }
    [[nodiscard]] auto View() const -> XrSpace { return view_; }

  private:
    explicit OpenXrSession(XrSession session);

    static auto CreateReferenceSpace(const OpenXrInstance& instance, XrSession session, XrReferenceSpaceType type)
        -> Result<XrSpace>;

    XrSession session_;
    XrSpace stage_ = XR_NULL_HANDLE;
    XrSpace view_ = XR_NULL_HANDLE;
};

struct OpenXrPendingDelete {
    std::unique_ptr<Overlay> overlay;
    OpenXrOverlay data;
    size_t frame = 0;
};

class OpenXrMainLoop final {
  public:
    static auto Create(std::unique_ptr<OpenXrInstance> instance, bool show_by_default) -> Result<std::unique_ptr<OpenXrMainLoop>>;

    auto Run(std::atomic<bool>& running) -> Result<>;

  private:
    explicit OpenXrMainLoop(std::unique_ptr<OpenXrInstance> instance);

    auto Initialize(bool show_by_default) -> Result<>;
    auto PollEvents() -> Result<bool>;
    auto HandleVisibility(bool visible) -> void;
    auto UpdateFps() -> float;
    auto EndFrame(std::span<const XrCompositionLayerBaseHeader* const> layers) -> Result<>;
    auto UpdateInput() -> Result<>;
    auto UpdateIpd(XrTime display_time) -> Result<>;
    auto RenderOverlays() -> Result<>;
    auto ComposeLayers() -> Result<std::vector<std::pair<float, OpenXrLayer>>>;
    auto DispatchTasks(size_t frame) -> void;
    auto RunSystemTask(const SystemTask& task) -> void;
    auto QueueDelete(std::unique_ptr<Overlay> overlay, size_t frame) -> void;
    auto WatchOverlay() -> Overlay&;

    std::unique_ptr<OpenXrInstance> instance_;
    std::unique_ptr<AppState> app_;
    std::unique_ptr<OverlayContainer> overlays_;
    std::unique_ptr<NotificationManager> notifications_;
    std::unique_ptr<Monado> monado_;
    std::optional<OpenXrPlayspace> playspace_;
    std::optional<OpenXrBlocker> blocker_;
    std::unique_ptr<OpenXrSession> session_;
    OpenXrState xr_;
    std::unique_ptr<OpenXrLines> lines_;
    std::unique_ptr<OpenXrSkybox> skybox_;
    std::unique_ptr<OpenXrInput> input_;
    std::unordered_map<OverlayId, OpenXrOverlay> overlay_data_;
    std::vector<OpenXrPendingDelete> delete_queue_;
    XrEnvironmentBlendMode blend_mode_ = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    std::array<size_t, 2> pointer_lines_{};
    std::array<size_t, 2> editor_lines_{};
    OverlayId watch_id_{};
    bool session_running_ = false;
    bool main_session_visible_ = false;
    std::chrono::steady_clock::time_point next_device_update_;
    std::deque<std::chrono::steady_clock::time_point> fps_counter_;
    size_t frame_counter_ = 0;
};

class OpenXrBackend {
  public:
    static auto Run(std::atomic<bool>& running, bool show_by_default) -> BackendResult;
};
