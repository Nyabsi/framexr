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

#include <Eigen/Geometry>

#include "Error.hpp"
#include "FontCache.hpp"
#include "Notifications.hpp"
#include "View.hpp"
#include "Container.hpp"
#include "methods/LaserPointer.hpp"
#include "TaskQueue.hpp"

enum class FrameStatus : uint8_t {
    Quit,
    Skip,
    Ready,
};

enum class Line : uint8_t {
    LeftPointer,
    RightPointer,
    LeftGuide,
    RightGuide,
};

class Runtime {
  public:
    static auto Run(std::atomic<bool>& running) -> void;
    static auto Uninstall() -> void;
    static auto BeamPose(Eigen::Affine3f from, float length, const Eigen::Affine3f& hmd) -> Eigen::Affine3f;

    virtual ~Runtime() = default;
    Runtime(const Runtime&) = delete;
    auto operator=(const Runtime&) -> Runtime& = delete;

  protected:
    Runtime() = default;

    auto Start() -> Result<>;
    auto Stop() -> void;
    [[nodiscard]] auto Views() -> std::vector<std::unique_ptr<View>>& { return m_overlays->Views(); }
    [[nodiscard]] auto WatchOverlay() -> View&;
    [[nodiscard]] auto WatchId() const -> OverlayId { return m_watch_id; }
    [[nodiscard]] auto Frame() const -> size_t { return m_frame; }

    virtual auto Open() -> Result<> = 0;
    virtual auto BeginFrame(bool running) -> Result<FrameStatus> = 0;
    virtual auto UpdateDevices() -> void = 0;
    virtual auto UpdateInput() -> Result<> = 0;
    virtual auto ApplyHaptics(size_t pointer, const Haptics& haptics) -> void = 0;
    virtual auto DrawLine(Line line, const Eigen::Affine3f& pose, float length, size_t color) -> void = 0;
    virtual auto HideLine(Line line) -> void = 0;
    virtual auto EndFrame() -> Result<> = 0;
    virtual auto Release(std::unique_ptr<View> overlay) -> void = 0;
    virtual auto RunSystemTask(const SystemTask& task) -> void = 0;

  private:
    auto Loop(std::atomic<bool>& running) -> Result<>;
    static auto Drive(std::unique_ptr<Runtime> runtime, std::atomic<bool>& running) -> bool;

    auto DispatchTasks() -> void;
    auto Interact() -> void;

    std::unique_ptr<FontCache> m_fonts;
    std::unique_ptr<Container> m_overlays;
    std::unique_ptr<Notifications> m_notifications;
    OverlayId m_watch_id{};
    size_t m_frame = 0;
    std::chrono::steady_clock::time_point m_next_device_update = std::chrono::steady_clock::now();
};
