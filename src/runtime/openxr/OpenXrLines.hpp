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

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <Eigen/Geometry>

#include "CommandBatch.hpp"
#include "CommandBuffer.hpp"
#include "Error.hpp"
#include "OpenXrState.hpp"
#include "OpenXrSwapchain.hpp"
#include "Pipeline.hpp"

struct OpenXrLine
{
    size_t color = 0;
    XrPosef pose{};
    float length = 0.0f;
};

struct OpenXrLineContainer
{
    std::unique_ptr<OpenXrSwapchain> swapchain;
    std::optional<OpenXrLine> line;
};

class OpenXrLines final
{
  public:
    static auto Create() -> Result<std::unique_ptr<OpenXrLines>>;

    auto Allocate(const OpenXrState& xr) -> Result<size_t>;
    auto DrawFrom(size_t id, Eigen::Affine3f from, float length, size_t color, const Eigen::Affine3f& hmd) -> void;
    auto Render(CommandBatch& batch) -> Result<>;
    auto Present(const OpenXrState& xr) -> Result<std::vector<OpenXrLayer>>;

  private:
    explicit OpenXrLines(std::shared_ptr<Pipeline> pipeline);

    std::shared_ptr<Pipeline> m_pipeline;
    std::map<size_t, OpenXrLineContainer> m_lines;
    size_t m_next_id = 1;
};
