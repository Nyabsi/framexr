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

#include "Button.hpp"
#include "Canvas.hpp"
#include "Error.hpp"
#include "Label.hpp"
#include "ModularElements.hpp"
#include "ModularUiConfig.hpp"

class Image;

class Modular final {
  public:
    static auto CreateCanvas(const ModularUiConfig& config, float scale = 1.0f) -> Result<std::unique_ptr<Canvas>>;
    static auto Fill(
        Canvas& canvas,
        const ModularUiConfig& config,
        float scale,
        const Eigen::Vector2f& origin = Eigen::Vector2f::Zero()
    ) -> void;
    static auto FillArranged(Canvas& canvas, std::span<const ModularElement> arranged, float scale, const Eigen::Vector2f& origin)
        -> void;

  private:
    static auto SpriteFromPath(const std::string& path) -> Result<std::shared_ptr<Image>>;
    static auto Build(Canvas& canvas, const ModularElement& element, float scale, const Eigen::Vector2f& origin) -> void;
};
