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
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <openxr/openxr.h>

#include "Error.hpp"
#include "OpenXrClickAction.hpp"
#include "OpenXrState.hpp"
#include "Pointer.hpp"

struct OpenXrHandActions {
    XrAction pose = XR_NULL_HANDLE;
    XrAction scroll = XR_NULL_HANDLE;
    XrAction haptics = XR_NULL_HANDLE;
    OpenXrClickAction click;
    OpenXrClickAction grab;
    OpenXrClickAction alt_click;
    OpenXrClickAction space_drag;
    OpenXrClickAction space_rotate;
    OpenXrClickAction space_reset;
    OpenXrClickAction modifier_right;
    OpenXrClickAction modifier_middle;
    OpenXrClickAction move_mouse;
};

class OpenXrHand {
  public:
    [[nodiscard]] auto Actions() const -> const OpenXrHandActions& { return m_actions; }
    [[nodiscard]] auto Actions() -> OpenXrHandActions& { return m_actions; }
    [[nodiscard]] auto Space() const -> const XrSpace& { return m_space; }
    [[nodiscard]] auto Space() -> XrSpace& { return m_space; }

    auto SetActions(OpenXrHandActions actions) -> void { m_actions = std::move(actions); }

    auto Update(Pointer& pointer, const OpenXrState& xr) -> Result<>;

  private:
    OpenXrHandActions m_actions;
    XrSpace m_space = XR_NULL_HANDLE;
};
