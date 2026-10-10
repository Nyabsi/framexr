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

#include <memory>
#include <vector>

#include "CommandBuffer.hpp"
#include "Error.hpp"
#include "Image.hpp"
#include "OpenXrState.hpp"
#include "OpenXrSwapchain.hpp"

class AppState;

class OpenXrSkybox final {
  public:
    static auto Create(const OpenXrState& xr, AppState& app) -> std::unique_ptr<OpenXrSkybox>;

    auto Render(const OpenXrState& xr, AppState& app, CommandBatch& batch) -> Result<>;
    auto Present(const OpenXrState& xr, const AppState& app) -> Result<std::vector<OpenXrLayer>>;

  private:
    explicit OpenXrSkybox(std::shared_ptr<Image> texture);

    static auto Load(AppState& app) -> Result<std::unique_ptr<OpenXrSkybox>>;

    auto PrepareSky(const OpenXrState& xr, AppState& app, CommandBatch& batch) -> Result<>;
    auto PrepareGrid(const OpenXrState& xr, AppState& app, CommandBatch& batch) -> Result<>;

    std::shared_ptr<Image> texture_;
    std::unique_ptr<OpenXrSwapchain> sky_;
    std::unique_ptr<OpenXrSwapchain> grid_;
};
