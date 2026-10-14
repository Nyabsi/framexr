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
#include "OpenXrState.hpp"

class OpenXrInstance;

class OpenXrMultiClick final
{
  public:
    OpenXrMultiClick() = default;

    static auto Create(
        const OpenXrInstance& instance,
        XrActionSet action_set,
        std::string_view action_name,
        std::string_view side,
        size_t count
    ) -> Result<OpenXrMultiClick>;

    [[nodiscard]] auto BoolAction() const -> XrAction { return m_bool_action; }
    [[nodiscard]] auto FloatAction() const -> XrAction { return m_float_action; }

    auto Check(const OpenXrState& xr, float threshold) -> Result<bool>;

  private:
    OpenXrMultiClick(std::string name, XrAction bool_action, XrAction float_action, size_t count);

    std::string m_name;
    XrAction m_bool_action = XR_NULL_HANDLE;
    XrAction m_float_action = XR_NULL_HANDLE;
    size_t m_count = 0;
    std::vector<std::chrono::steady_clock::time_point> m_previous;
    bool m_held_active = false;
    bool m_held_inactive = false;
};
