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

#include "Capture.hpp"
#include "CapturedFrame.hpp"
#include "CommandBatch.hpp"
#include "Error.hpp"
#include "FrameUploader.hpp"
#include "Compose.hpp"
#include "ScreenPipeline.hpp"

class Image;

class ScreenRenderer final
{
  public:
    ScreenRenderer(std::string name, std::unique_ptr<Capture> capture);

    auto Pause() -> Result<>;
    auto Resume() -> Result<>;
    auto ShouldRender() -> Result<RenderReadiness>;
    auto Render(const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool>;
    auto Meta() -> std::optional<FrameMeta>;
    auto SetTexelFootprint(const TexelFootprint& footprint) -> void;

  private:
    auto Start() -> void;

    std::string m_name;
    std::unique_ptr<Capture> m_capture;
    std::shared_ptr<FrameUploader> m_uploader;
    bool m_main_thread = false;
    std::optional<ScreenPipeline> m_pipeline;
    std::unique_ptr<CapturedFrame> m_current;
    std::optional<FrameMeta> m_meta;
    TexelFootprint m_footprint;
};
