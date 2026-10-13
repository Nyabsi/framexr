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
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Error.hpp"

struct InputBinding {
    std::optional<std::string> left;
    std::optional<std::string> right;
    std::optional<std::array<float, 2>> threshold;
    std::optional<bool> double_click;
    std::optional<bool> triple_click;
};

struct BindingProfile {
    std::string profile;
    std::optional<InputBinding> pose;
    std::optional<InputBinding> click;
    std::optional<InputBinding> grab;
    std::optional<InputBinding> alt_click;
    std::optional<InputBinding> space_drag;
    std::optional<InputBinding> space_rotate;
    std::optional<InputBinding> space_reset;
    std::optional<InputBinding> click_modifier_right;
    std::optional<InputBinding> click_modifier_middle;
    std::optional<InputBinding> move_mouse;
    std::optional<InputBinding> scroll;
    std::optional<InputBinding> haptic;
};

class Bindings final {
  public:
    static auto Load() -> std::vector<BindingProfile>;
    static auto Parse(const nlohmann::json& json) -> Result<std::vector<BindingProfile>>;
};
