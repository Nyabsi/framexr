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
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <vulkan/vulkan.h>

#include "Frame.hpp"
#include "methods/LaserPointer.hpp"

class ScreenInteraction final {
  public:
    ScreenInteraction(const Eigen::Vector2f& position, const Eigen::Vector2f& size, FrameTransform transform);

    auto Hover(const PointerHit& hit) -> void;
    auto Press(const PointerHit& hit, bool pressed) -> void;
    static auto Scroll(float delta_y, float delta_x) -> void;

  private:
    Eigen::Affine2f m_mouse_transform;
    std::array<std::optional<uint16_t>, 2> m_held{};
};
