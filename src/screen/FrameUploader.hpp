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

#include "CapturedFrame.hpp"
#include "Frame.hpp"

class Image;
class Queue;

class FrameUploader final {
  public:
    FrameUploader(std::string name, Queue& queue);

    auto Process(Frame&& frame) const -> std::unique_ptr<CapturedFrame>;

  private:
    static auto HasValidPlanes(const DmabufFrame& frame) -> bool;

    [[nodiscard]] auto Upload(const FrameFormat& format, std::span<const std::byte> data, int32_t stride) const
        -> std::shared_ptr<Image>;

    std::string m_name;
    Queue* m_queue;
};
