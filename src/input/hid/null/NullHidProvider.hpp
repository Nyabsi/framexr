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

#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <Eigen/Core>

#include "HidProvider.hpp"
#include "VirtualKeys.hpp"

class NullHidProvider final : public HidProvider {
  public:
    auto MouseMove(const Eigen::Vector2f& position) -> void override;
    auto SendButton(uint16_t button, bool down) -> void override;
    auto Wheel(int32_t delta_y, int32_t delta_x) -> void override;
    auto SetModifiers(uint8_t modifiers) -> void override;
    auto SendKey(VirtualKey key, bool down) -> void override;
    auto SetDesktopExtent(const Eigen::Vector2f& extent) -> void override;
    auto SetDesktopOrigin(const Eigen::Vector2f& origin) -> void override;
    auto Commit() -> void override;
    auto SelectInputMethod(ImeScript script) -> void override;
};
