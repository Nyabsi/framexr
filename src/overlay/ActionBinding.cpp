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

#include "ActionBinding.hpp"

#include <memory>

#include "Config.hpp"

auto ActionBinding::Bind(FormComponent& button, const ButtonData& data) -> void
{
    auto binding = std::make_shared<ActionBinding>(data);
    button.SetOnPress([binding](PointerMode mode) -> void { binding->Press(mode); });
    button.SetOnRelease([binding]() -> void { binding->Release(); });
    button.SetOnScroll([binding](float delta_y, float) -> void { binding->Scroll(delta_y); });
    button.SetHighlightColor([binding]() -> std::optional<Eigen::Vector4f> { return binding->Highlight(); });
}

ActionBinding::ActionBinding(ButtonData data) : m_data(std::move(data)), m_last_down(std::chrono::steady_clock::now())
{
}

auto ActionBinding::Press(PointerMode mode) -> void
{
    m_last_down = std::chrono::steady_clock::now();
    m_last_mode = mode;
    switch (mode)
    {
    case PointerMode::Left: Run(m_data.click_down); break;
    case PointerMode::Right: Run(m_data.right_down); break;
    case PointerMode::Middle: Run(m_data.middle_down); break;
    case PointerMode::Special: break;
    }
}

auto ActionBinding::Release() -> void
{
    const auto held = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_last_down).count();
    const bool long_press = held > g_config->long_press_duration;
    switch (m_last_mode)
    {
    case PointerMode::Left: Run(long_press ? m_data.long_click_up : m_data.click_up); break;
    case PointerMode::Right: Run(long_press ? m_data.long_right_up : m_data.right_up); break;
    case PointerMode::Middle: Run(long_press ? m_data.long_middle_up : m_data.middle_up); break;
    case PointerMode::Special: break;
    }
}

auto ActionBinding::Scroll(float delta_y) -> void
{
    Run(delta_y < 0.0f ? m_data.scroll_down : m_data.scroll_up);
}

auto ActionBinding::Highlight() const -> std::optional<Eigen::Vector4f>
{
    if (!m_data.highlight)
    {
        return std::nullopt;
    }
    bool lit = false;
    switch (*m_data.highlight)
    {
    case HighlightTest::AllowSliding: lit = g_config->allow_sliding; break;
    case HighlightTest::NotificationSounds: lit = g_config->notifications_sound_enabled; break;
    case HighlightTest::Notifications: lit = g_config->notifications_enabled; break;
    case HighlightTest::RotateLock: lit = !g_config->space_rotate_unlocked; break;
    }
    if (lit)
    {
        return Eigen::Vector4f(1.0f, 1.0f, 1.0f, 0.5f);
    }
    return std::nullopt;
}

auto ActionBinding::Run(const std::optional<std::vector<ButtonAction>>& actions) -> void
{
    if (actions)
    {
        m_runner.Run(*actions);
    }
}
