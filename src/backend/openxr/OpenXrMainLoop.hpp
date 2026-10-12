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

#include "Device.hpp"
#include "Error.hpp"
#include "Monado.hpp"
#include "Notifications.hpp"
#include "OpenXrBlocker.hpp"
#include "OpenXrInput.hpp"
#include "OpenXrInstance.hpp"
#include "OpenXrLines.hpp"
#include "OpenXrOverlay.hpp"
#include "OpenXrPlayspace.hpp"
#include "OpenXrSession.hpp"
#include "OpenXrSkybox.hpp"
#include "OpenXrState.hpp"
#include "Overlay.hpp"
#include "OverlayContainer.hpp"
#include "Pointer.hpp"
#include "State.hpp"
#include "Task.hpp"

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

    std::unique_ptr<OpenXrInstance> m_instance;
    std::unique_ptr<Device> m_device;
    std::unique_ptr<AppState> m_app;
    std::unique_ptr<OverlayContainer> m_overlays;
    std::unique_ptr<NotificationManager> m_notifications;
    std::unique_ptr<Monado> m_monado;
    std::optional<OpenXrPlayspace> m_playspace;
    std::optional<OpenXrBlocker> m_blocker;
    std::unique_ptr<OpenXrSession> m_session;
    OpenXrState m_xr;
    std::unique_ptr<OpenXrLines> m_lines;
    std::unique_ptr<OpenXrSkybox> m_skybox;
    std::unique_ptr<OpenXrInput> m_input;
    std::unordered_map<OverlayId, OpenXrOverlay> m_overlay_data;
    std::vector<OpenXrPendingDelete> m_delete_queue;
    XrEnvironmentBlendMode m_blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    std::array<size_t, 2> m_pointer_lines{};
    std::array<size_t, 2> m_editor_lines{};
    OverlayId m_watch_id{};
    bool m_session_running = false;
    bool m_main_session_visible = false;
    std::chrono::steady_clock::time_point m_next_device_update;
    std::deque<std::chrono::steady_clock::time_point> m_fps_counter;
    size_t m_frame_counter = 0;
};
