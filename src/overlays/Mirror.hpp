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

#include "Overlay.hpp"
#include "Screen.hpp"

class AppState;
struct Config;

class MirrorRenderer final : public OverlayRenderer {
  public:
    explicit MirrorRenderer(std::string name);

    static auto Create(std::string name, bool show_hide, const Config& config)
        -> std::pair<OverlayState, std::unique_ptr<OverlayBackend>>;

    auto Init(AppState& app) -> Result<> override;
    auto Pause(AppState& app) -> Result<> override;
    auto Resume(AppState& app) -> Result<> override;
    auto ShouldRender(AppState& app) -> Result<RenderReadiness> override;
    auto Render(AppState& app, const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> override;
    auto Meta() -> std::optional<FrameMeta> override;

  private:
    std::string name_;
    std::unique_ptr<ScreenRenderer> renderer_;
    std::optional<std::future<Result<ScreenSelection>>> selection_;
    VkExtent2D last_extent_{};
};
