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
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "ModularUiConfig.hpp"

class ModularElements final {
  public:
    static auto Rect(const ModularElement& element) -> const std::array<float, 4>&;
    static auto Rect(ModularElement& element) -> std::array<float, 4>&;
    static auto RegionOf(const ModularElement& element) -> Region;
    static auto SetRegion(ModularElement& element, Region region) -> void;
    static auto FormOf(const ModularElement& element) -> size_t;
    static auto SetForm(ModularElement& element, size_t form) -> void;
    static auto TypeName(const ModularElement& element) -> std::string_view;
};
