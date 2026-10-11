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

#include "Backend.hpp"
#include "Device.hpp"
#include "Error.hpp"
#include "Notifications.hpp"
#include "OpenVrInput.hpp"
#include "OpenVrLines.hpp"
#include "OpenVrOverlay.hpp"
#include "OpenVrPlayspace.hpp"
#include "OverlayContainer.hpp"
#include "State.hpp"
#include "Task.hpp"

class OpenVrRuntime final {
  public:
    OpenVrRuntime() = default;
    ~OpenVrRuntime();
    OpenVrRuntime(const OpenVrRuntime&) = delete;
    auto operator=(const OpenVrRuntime&) -> OpenVrRuntime& = delete;

    auto Retain(std::shared_ptr<Device> device) -> void { device_ = std::move(device); }

  private:
    std::shared_ptr<Device> device_;
};

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

    std::unique_ptr<AppState> app_;
    std::unique_ptr<OverlayContainer> overlays_;
    std::unordered_map<OverlayId, OpenVrOverlayData> overlay_data_;
    std::unique_ptr<NotificationManager> notifications_;
    OpenVrPlayspace playspace_;
    OpenVrInput input_;
    OpenVrLines lines_;
    std::array<size_t, 2> pointer_lines_{};
    std::array<size_t, 2> editor_lines_{};
    OverlayId watch_id_{};
    uint32_t frame_timeout_ = 0;
    std::chrono::steady_clock::time_point next_device_update_;
    size_t frame_counter_ = 0;
};

class OpenVrBackend {
  public:
    static auto Run(std::atomic<bool>& running, bool show_by_default) -> BackendResult;
    static auto Uninstall() -> void;
};
