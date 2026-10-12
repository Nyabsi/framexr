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

#include "OpenXrClickAction.hpp"

#include <cstring>
#include <utility>

#include "Config.hpp"
#include "Input.hpp"
#include "OpenXrInstance.hpp"

auto OpenXrClickAction::Create(const OpenXrInstance& instance, XrActionSet action_set, std::string_view name, std::string_view side)
    -> Result<OpenXrClickAction> {
    std::array<OpenXrMultiClick, 3> handlers;
    for (size_t count = 0; count < handlers.size(); ++count) {
        ASSIGN_OR_RETURN(handlers.at(count), OpenXrMultiClick::Create(instance, action_set, name, side, count));
    }
    return OpenXrClickAction(std::move(handlers));
}

OpenXrClickAction::OpenXrClickAction(std::array<OpenXrMultiClick, 3> handlers) : m_handlers(std::move(handlers)) {
}

auto OpenXrClickAction::State(bool before, const OpenXrState& xr) -> Result<bool> {
    const float threshold = before ? g_config->xr_click_sensitivity_release : g_config->xr_click_sensitivity;
    for (auto& handler : m_handlers) {
        ASSIGN_OR_RETURN(const bool pressed, handler.Check(xr, threshold));
        if (pressed) {
            return true;
        }
    }
    return false;
}
