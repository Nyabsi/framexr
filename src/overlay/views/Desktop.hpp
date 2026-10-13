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
#include <span>
#include <string>
#include <vector>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <vulkan/vulkan.h>

#include "Capture.hpp"
#include "CommandBatch.hpp"
#include "Error.hpp"
#include "Frame.hpp"
#include "View.hpp"
#include "Compose.hpp"
#include "ScreenCastPortal.hpp"
#include "ScreenInteraction.hpp"
#include "ScreenRenderer.hpp"

class WaylandClient;
struct WaylandOutput;

class Desktop final : public View {
  public:
    Desktop(
        std::string name,
        const Eigen::Vector2i& resolution,
        FrameTransform transform,
        const Eigen::Vector2i& position,
        std::unique_ptr<ScreenRenderer> screen,
        ScreenInteraction input
    );

    static auto CreateWayland(WaylandClient& client, std::span<Desktop* const> existing) -> std::vector<std::unique_ptr<Desktop>>;
    static auto CreateCompose(
        const WaylandOutput& output,
        bool has_wlr_dmabuf,
        bool has_wlr_screencopy,
        std::map<std::string, std::string>& tokens
    ) -> std::unique_ptr<ScreenRenderer>;
    static auto CreateX11Pipewire() -> Result<std::vector<std::unique_ptr<Desktop>>>;
    static auto CreateXshm() -> Result<std::vector<std::unique_ptr<Desktop>>>;
#ifdef _WIN32
    static auto CreateWindows() -> Result<std::vector<std::unique_ptr<Desktop>>>;
#endif
    static auto ArrangeInFrontOfHead(std::span<const std::unique_ptr<Desktop>> desktops) -> void;
    static auto SelectScreen(std::string_view instructions, const ScreenCastOptions& options) -> Result<ScreenSelection>;

    [[nodiscard]] auto Output() const -> uint32_t { return m_output; }
    [[nodiscard]] auto DesktopPosition() const -> const Eigen::Vector2i& { return m_position; }

    auto SetOutput(uint32_t output) -> void { m_output = output; }
    auto SetInput(ScreenInteraction input) -> void { m_input = input; }

    auto OnHover(const PointerHit& hit) -> std::optional<Haptics> override;
    auto OnPointer(const PointerHit& hit, bool pressed) -> void override;
    auto OnScroll(const PointerHit& hit, float delta_y, float delta_x) -> void override;

  private:
    ScreenInteraction m_input;
    Eigen::Vector2i m_position;
    uint32_t m_output = 0;
};
