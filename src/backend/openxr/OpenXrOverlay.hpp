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
#include <optional>

#include "Error.hpp"
#include "OpenXrState.hpp"
#include "OpenXrSwapchain.hpp"

class AppState;
class Overlay;

struct OpenXrOverlay {
    bool last_visible = false;
    std::unique_ptr<OpenXrSwapchain> swapchain;
    bool init = false;
    bool cur_visible = false;
    float last_alpha = 0.0f;

    auto EnsureSwapchain(Overlay& overlay, AppState& app, const OpenXrState& xr) -> Result<bool>;
    auto Present(Overlay& overlay, const OpenXrState& xr) -> Result<std::optional<OpenXrLayer>>;
    auto AfterInput(Overlay& overlay, AppState& app) -> Result<>;
};
