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

#include "CommandBatch.hpp"
#include "CommandBuffer.hpp"
#include "Error.hpp"
#include "Image.hpp"
#include "OpenXrState.hpp"
#include "OpenXrSwapchain.hpp"

class OpenXrSkybox final {
  public:
    static auto Create(const OpenXrState& xr) -> std::unique_ptr<OpenXrSkybox>;

    auto Render(const OpenXrState& xr, CommandBatch& batch) -> Result<>;
    auto Present(const OpenXrState& xr) -> Result<std::vector<OpenXrLayer>>;

  private:
    explicit OpenXrSkybox(std::shared_ptr<Image> texture);

    static auto Load() -> Result<std::unique_ptr<OpenXrSkybox>>;

    auto PrepareSky(const OpenXrState& xr, CommandBatch& batch) -> Result<>;
    auto PrepareGrid(const OpenXrState& xr, CommandBatch& batch) -> Result<>;

    std::shared_ptr<Image> m_texture;
    std::unique_ptr<OpenXrSwapchain> m_sky;
    std::unique_ptr<OpenXrSwapchain> m_grid;
};
