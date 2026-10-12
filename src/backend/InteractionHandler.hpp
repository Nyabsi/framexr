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

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <Eigen/Geometry>
#include <vulkan/vulkan.h>

#include "Input.hpp"
#include "Pointer.hpp"

class InteractionHandler {
  public:
    virtual ~InteractionHandler() = default;

    virtual auto OnHover(const PointerHit& hit) -> std::optional<Haptics> = 0;
    virtual auto OnLeft(size_t pointer) -> void = 0;
    virtual auto OnPointer(const PointerHit& hit, bool pressed) -> void = 0;
    virtual auto OnScroll(const PointerHit& hit, float delta_y, float delta_x) -> void = 0;
    [[nodiscard]] virtual auto Accepts(const Eigen::Vector2f& uv) const -> bool;
};
