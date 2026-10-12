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

#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "CommandBatch.hpp"
#include "Overlay.hpp"
#include "OverlayRenderer.hpp"
#include "Screen.hpp"
#include "ScreenRenderer.hpp"

class MirrorRenderer final : public OverlayRenderer {
  public:
    explicit MirrorRenderer(std::string name);

    static auto Create(std::string name, bool show_hide) -> std::pair<OverlayState, std::unique_ptr<OverlayBackend>>;

    auto Init() -> Result<> override;
    auto Pause() -> Result<> override;
    auto Resume() -> Result<> override;
    auto ShouldRender() -> Result<RenderReadiness> override;
    auto Render(const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> override;
    auto Meta() -> std::optional<FrameMeta> override;

  private:
    std::string m_name;
    std::unique_ptr<ScreenRenderer> m_renderer;
    std::optional<std::future<Result<ScreenSelection>>> m_selection;
    VkExtent2D m_last_extent{};
};
