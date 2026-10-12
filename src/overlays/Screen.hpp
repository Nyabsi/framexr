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
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <vulkan/vulkan.h>

#include "Capture.hpp"
#include "CommandBatch.hpp"
#include "Error.hpp"
#include "Frame.hpp"
#include "Overlay.hpp"
#include "OverlayContainer.hpp"
#include "OverlayRenderer.hpp"
#include "ScreenCastPortal.hpp"
#include "ScreenRenderer.hpp"

class WaylandClient;

struct WaylandOutput;

struct ScreenOverlay {
    ScreenMeta meta;
    OverlayState state;
    std::unique_ptr<OverlayBackend> backend;
    Eigen::Vector2i desktop_position = Eigen::Vector2i::Zero();
};

class Screens final {
  public:
    static auto CreateWayland(WaylandClient& client) -> std::vector<ScreenOverlay>;
    static auto CreateWaylandRenderer(
        const WaylandOutput& output,
        bool has_wlr_dmabuf,
        bool has_wlr_screencopy,
        std::map<std::string, std::string>& tokens
    ) -> std::unique_ptr<ScreenRenderer>;
    static auto CreateX11Pipewire() -> Result<std::vector<ScreenOverlay>>;
    static auto CreateXshm() -> Result<std::vector<ScreenOverlay>>;
#ifdef _WIN32
    static auto CreateWindows() -> Result<std::vector<ScreenOverlay>>;
#endif
    static auto ArrangeInFrontOfHead(std::vector<ScreenOverlay>& screens) -> void;
    static auto SelectScreen(std::string_view instructions, const ScreenCastOptions& options) -> Result<ScreenSelection>;
    static auto LoadTokens() -> Result<std::map<std::string, std::string>>;
    static auto SaveTokens(const std::map<std::string, std::string>& tokens) -> Result<>;

  private:
    static auto CreateState(std::string name, const Eigen::Vector2i& resolution, FrameTransform transform) -> OverlayState;
};
