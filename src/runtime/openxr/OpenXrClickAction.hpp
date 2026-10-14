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
#include "OpenXrMultiClick.hpp"
#include "OpenXrState.hpp"

class OpenXrInstance;

class OpenXrClickAction final
{
  public:
    OpenXrClickAction() = default;

    static auto Create(const OpenXrInstance& instance, XrActionSet action_set, std::string_view name, std::string_view side)
        -> Result<OpenXrClickAction>;

    [[nodiscard]] auto Handler(size_t count) const -> const OpenXrMultiClick& { return m_handlers.at(count); }

    auto State(bool before, const OpenXrState& xr) -> Result<bool>;

  private:
    explicit OpenXrClickAction(std::array<OpenXrMultiClick, 3> handlers);

    std::array<OpenXrMultiClick, 3> m_handlers;
};
