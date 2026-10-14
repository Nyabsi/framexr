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
#include <memory>
#include <optional>
#include <vector>

#include <Eigen/Geometry>
#include <openvr.h>

#include "Device.hpp"
#include "Error.hpp"
#include "Image.hpp"
#include "OpenVrOverlay.hpp"
#include "View.hpp"
#include "Compose.hpp"

struct OpenVrLine
{
    size_t id = 0;
    std::unique_ptr<View> overlay;
    OpenVrOverlayData data;
};

class OpenVrLines final
{
  public:
    static auto Create() -> Result<OpenVrLines>;

    auto Allocate() -> size_t;
    auto DrawFrom(size_t id, Eigen::Affine3f from, float length, size_t color, const Eigen::Affine3f& hmd) -> void;
    auto Hide(size_t id) -> void;
    auto Update(vr::ETrackingUniverseOrigin universe) -> Result<>;
    auto MarkDirty() -> void;

  private:
    explicit OpenVrLines(std::shared_ptr<Image> image);

    std::shared_ptr<Image> m_image;
    std::vector<OpenVrLine> m_lines;
    std::array<Eigen::Vector4f, 5> m_colors;
};
