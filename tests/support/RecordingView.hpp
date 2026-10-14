// FrameXR
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
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
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <Eigen/Core>
#include <vulkan/vulkan.h>

#include "Interaction.hpp"
#include "Compose.hpp"
#include "View.hpp"

enum class RecordedKind : uint8_t
{
    Hover,
    Left,
    Press,
    Scroll,
};

struct RecordedEvent
{
    RecordedKind kind = RecordedKind::Hover;
    size_t pointer = 0;
    Eigen::Vector2f uv = Eigen::Vector2f::Zero();
    bool pressed = false;
    PointerMode mode = PointerMode::Left;
    bool primary = false;
    float delta_y = 0.0f;
    float delta_x = 0.0f;
};

class RecordingView final : public View
{
  public:
    RecordingView(VkExtent2D extent, std::shared_ptr<std::vector<RecordedEvent>> events);

    auto OnHover(const PointerHit& hit) -> std::optional<Haptics> override;
    auto OnLeft(size_t pointer) -> void override;
    auto OnPointer(const PointerHit& hit, bool pressed) -> void override;
    auto OnScroll(const PointerHit& hit, float delta_y, float delta_x) -> void override;

  private:
    std::shared_ptr<std::vector<RecordedEvent>> m_events;
};
