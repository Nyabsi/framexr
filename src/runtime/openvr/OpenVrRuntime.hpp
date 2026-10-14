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
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

#include "Device.hpp"
#include "OpenVrInput.hpp"
#include "OpenVrLines.hpp"
#include "OpenVrOverlay.hpp"
#include "OpenVrPlayspace.hpp"
#include "Runtime.hpp"

class OpenVrRuntime final : public Runtime
{
  public:
    static auto Connect() -> std::unique_ptr<OpenVrRuntime>;
    static auto Uninstall() -> void;

    ~OpenVrRuntime() override;
    OpenVrRuntime(const OpenVrRuntime&) = delete;
    auto operator=(const OpenVrRuntime&) -> OpenVrRuntime& = delete;

  protected:
    auto Open() -> Result<> override;
    auto BeginFrame(bool running) -> Result<FrameStatus> override;
    auto UpdateDevices() -> void override;
    auto UpdateInput() -> Result<> override;
    auto ApplyHaptics(size_t pointer, const Haptics& haptics) -> void override;
    auto DrawLine(Line line, const Eigen::Affine3f& pose, float length, size_t color) -> void override;
    auto HideLine(Line line) -> void override;
    auto EndFrame() -> Result<> override;
    auto Release(std::unique_ptr<View> overlay) -> void override;
    auto RunSystemTask(const SystemTask& task) -> void override;

  private:
    OpenVrRuntime() = default;

    auto PollEvents() -> bool;
    static auto AdjustGain(ColorChannel channel, float delta) -> void;
    static auto ReadIpd() -> std::optional<float>;
    static auto ReadDisplayDensity() -> Eigen::Vector2f;

    std::unique_ptr<Device> m_device;
    std::unordered_map<OverlayId, OpenVrOverlayData> m_overlay_data;
    OpenVrPlayspace m_playspace;
    std::optional<OpenVrInput> m_input;
    std::optional<OpenVrLines> m_lines;
    std::array<size_t, 4> m_line_ids{};
    uint32_t m_frame_timeout = 0;
};
