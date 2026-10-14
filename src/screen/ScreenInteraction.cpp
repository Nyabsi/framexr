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

#include "ScreenInteraction.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <format>
#include <utility>

#include "Config.hpp"
#include "HidProvider.hpp"
#include "Interaction.hpp"
#include "FrameTransforms.hpp"
#include "Logger.hpp"
#include "framexr_config.h"

namespace
{

std::atomic<int64_t> next_move_ms{0};

const auto kStart = std::chrono::steady_clock::now();

auto ElapsedMs() -> int64_t
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - kStart).count();
}

auto CanMove() -> bool
{
    return ElapsedMs() > next_move_ms.load(std::memory_order_relaxed);
}

auto SetNextMove(uint32_t millis_from_now) -> void
{
    next_move_ms.store(ElapsedMs() + millis_from_now, std::memory_order_relaxed);
}

} // namespace

ScreenInteraction::ScreenInteraction(const Eigen::Vector2f& position, const Eigen::Vector2f& size, FrameTransform transform)
{
    if (FrameTransforms::IsRotated90(transform))
    {
        m_mouse_transform = FrameTransforms::Affine({0.0f, size.y()}, {-size.x(), 0.0f}, {position.x() + size.x(), position.y()});
    }
    else if (FrameTransforms::IsRotated180(transform))
    {
        m_mouse_transform = FrameTransforms::Affine({-size.x(), 0.0f}, {0.0f, -size.y()}, position + size);
    }
    else if (FrameTransforms::IsRotated270(transform))
    {
        m_mouse_transform = FrameTransforms::Affine({0.0f, -size.y()}, {size.x(), 0.0f}, {position.x(), position.y() + size.y()});
    }
    else
    {
        m_mouse_transform = FrameTransforms::Affine({size.x(), 0.0f}, {0.0f, size.y()}, position);
    }
}

auto ScreenInteraction::Hover(const PointerHit& hit) -> void
{
    LOG_TRACE("Hover: [{}, {}]", hit.uv.x(), hit.uv.y());
    if (CanMove() && (!g_config->focus_follows_mouse_mode || g_interaction->Pointers()[hit.pointer].Now().move_mouse))
    {
        g_hid->MouseMove(m_mouse_transform * hit.uv);
        SetNextMove(g_config->mouse_move_interval_ms);
    }
}

auto ScreenInteraction::Press(const PointerHit& hit, bool pressed) -> void
{
    uint16_t button = kMouseLeft;
    if (hit.mode == PointerMode::Right)
    {
        button = kMouseRight;
    }
    else if (hit.mode == PointerMode::Middle)
    {
        button = kMouseMiddle;
    }

    if (hit.pointer < m_held.size())
    {
        if (pressed)
        {
            m_held[hit.pointer] = button;
        }
        else if (const auto held = std::exchange(m_held[hit.pointer], std::nullopt))
        {
            button = *held;
        }
    }

    if (pressed)
    {
        SetNextMove(g_config->click_freeze_time_ms);
    }

    g_hid->SendButton(button, pressed);

    if (pressed)
    {
        g_hid->MouseMove(m_mouse_transform * hit.uv);
    }
}

auto ScreenInteraction::Scroll(float delta_y, float delta_x) -> void
{
    g_hid->Wheel(static_cast<int32_t>(delta_y * 64.0f), static_cast<int32_t>(delta_x * 64.0f));
}
