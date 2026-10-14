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

#include <openxr/openxr.h>

#include "Error.hpp"
#include "Interaction.hpp"
#include "OpenXrHand.hpp"
#include "OpenXrState.hpp"

class Monado;

class OpenXrInstance;

class OpenXrInput final
{
  public:
    static auto Create(const OpenXrState& xr) -> Result<std::unique_ptr<OpenXrInput>>;
    static auto UpdateDevices(Monado& monado) -> void;

    ~OpenXrInput();
    OpenXrInput(const OpenXrInput&) = delete;
    auto operator=(const OpenXrInput&) -> OpenXrInput& = delete;

    auto Update(const OpenXrState& xr) -> Result<>;
    auto ApplyHaptics(const OpenXrState& xr, size_t hand, const Haptics& haptics) -> void;

  private:
    OpenXrInput(XrActionSet action_set, std::array<OpenXrHand, 2> hands);

    static auto CreateHandActions(const OpenXrInstance& instance, XrActionSet action_set, std::string_view side)
        -> Result<OpenXrHandActions>;
    static auto SuggestBindings(const OpenXrInstance& instance, const std::array<OpenXrHandActions, 2>& hands) -> void;

    XrActionSet m_action_set;
    std::array<OpenXrHand, 2> m_hands;
};
