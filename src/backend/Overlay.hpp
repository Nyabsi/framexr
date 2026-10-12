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

#include "CommandBuffer.hpp"
#include "Error.hpp"
#include "Image.hpp"
#include "Input.hpp"
#include "OverlayBackend.hpp"
#include "OverlayState.hpp"

class Overlay {
  public:
    Overlay(OverlayState state, std::unique_ptr<OverlayBackend> backend);

    [[nodiscard]] auto State() const -> const OverlayState& { return m_state; }
    [[nodiscard]] auto State() -> OverlayState& { return m_state; }
    [[nodiscard]] auto Backend() const -> OverlayBackend* { return m_backend.get(); }
    [[nodiscard]] auto TakeBackend() -> std::unique_ptr<OverlayBackend> { return std::move(m_backend); }

    auto SetBackend(std::unique_ptr<OverlayBackend> backend) -> void { m_backend = std::move(backend); }
    [[nodiscard]] auto PrimaryPointer() const -> const std::optional<size_t>& { return m_primary_pointer; }
    [[nodiscard]] auto PrimaryPointer() -> std::optional<size_t>& { return m_primary_pointer; }

    auto SetPrimaryPointer(std::optional<size_t> primary_pointer) -> void { m_primary_pointer = primary_pointer; }

    auto Init() -> Result<>;

  private:
    OverlayState m_state;
    std::unique_ptr<OverlayBackend> m_backend;
    std::optional<size_t> m_primary_pointer;
};
