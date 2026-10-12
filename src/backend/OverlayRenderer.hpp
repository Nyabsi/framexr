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

#include "CommandBatch.hpp"
#include "CommandBuffer.hpp"
#include "Error.hpp"
#include "Image.hpp"

enum class RenderReadiness : uint8_t {
    Should,
    Can,
    Unable,
};

struct TexelFootprint {
    Eigen::Vector3f eye = Eigen::Vector3f::UnitZ();
    Eigen::Vector2f size = Eigen::Vector2f::Ones();
    Eigen::Vector2f display_pixels_per_radian = Eigen::Vector2f::Zero();
};

struct FrameMeta {
    VkExtent2D extent{};
    Eigen::Affine3f transform = Eigen::Affine3f::Identity();
    VkFormat format = VK_FORMAT_UNDEFINED;
};

class OverlayRenderer {
  public:
    virtual ~OverlayRenderer() = default;

    virtual auto Init() -> Result<> = 0;
    virtual auto Pause() -> Result<> = 0;
    virtual auto Resume() -> Result<> = 0;
    virtual auto ShouldRender() -> Result<RenderReadiness> = 0;
    virtual auto Render(const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> = 0;
    virtual auto Meta() -> std::optional<FrameMeta> = 0;
    virtual auto SetTexelFootprint(const TexelFootprint& footprint) -> void;
};
