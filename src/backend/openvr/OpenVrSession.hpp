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
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

#include "Error.hpp"
#include "Notifications.hpp"
#include "OpenVrInput.hpp"
#include "OpenVrLines.hpp"
#include "OpenVrOverlay.hpp"
#include "OpenVrPlayspace.hpp"
#include "OpenVrRuntime.hpp"
#include "Overlay.hpp"
#include "OverlayContainer.hpp"
#include "Pointer.hpp"
#include "State.hpp"
#include "Task.hpp"

class OpenVrSession final {
  public:
    static auto Create(OpenVrRuntime& runtime, bool show_by_default) -> Result<std::unique_ptr<OpenVrSession>>;

    auto Run(std::atomic<bool>& running) -> Result<>;

  private:
    OpenVrSession(
        std::unique_ptr<AppState> app,
        std::unique_ptr<OverlayContainer> overlays,
        OpenVrInput input,
        OpenVrLines lines,
        uint32_t frame_timeout
    );

    auto PollEvents() -> bool;
    auto DispatchTasks(size_t frame) -> void;
    auto RunSystemTask(const SystemTask& task) -> void;
    auto UpdateInput() -> void;
    auto RenderOverlays() -> Result<>;
    auto DestroyOverlay(const Overlay& overlay) -> void;
    auto DataFor(OverlayId id) -> OpenVrOverlayData&;

    static auto AdjustGain(ColorChannel channel, float delta) -> void;
    static auto ReadIpd() -> std::optional<float>;
    static auto ReadDisplayDensity() -> Eigen::Vector2f;

    std::unique_ptr<AppState> m_app;
    std::unique_ptr<OverlayContainer> m_overlays;
    std::unordered_map<OverlayId, OpenVrOverlayData> m_overlay_data;
    std::unique_ptr<NotificationManager> m_notifications;
    OpenVrPlayspace m_playspace;
    OpenVrInput m_input;
    OpenVrLines m_lines;
    std::array<size_t, 2> m_pointer_lines{};
    std::array<size_t, 2> m_editor_lines{};
    OverlayId m_watch_id{};
    uint32_t m_frame_timeout = 0;
    std::chrono::steady_clock::time_point m_next_device_update;
    size_t m_frame_counter = 0;
};
