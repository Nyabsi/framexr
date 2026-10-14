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

#include "SendInputProvider.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "HidProvider.hpp"
#include "Logger.hpp"
#include "Scancodes.hpp"

namespace
{

constexpr float kAbsoluteExtent = 65535.0f;

auto KeyInput(VirtualKey key, bool down) -> std::optional<INPUT>
{
    INPUT input{.type = INPUT_KEYBOARD};
    const DWORD release = down ? 0 : KEYEVENTF_KEYUP;
    if (key == VirtualKey::Pause)
    {
        input.ki.wVk = VK_PAUSE;
        input.ki.dwFlags = release;
        return input;
    }
    const auto scancode = Scancodes::ForKey(key);
    if (!scancode)
    {
        return std::nullopt;
    }
    input.ki.wScan = static_cast<WORD>(*scancode & 0xFF);
    input.ki.dwFlags = KEYEVENTF_SCANCODE | release | ((*scancode & kScancodeExtended) != 0 ? KEYEVENTF_EXTENDEDKEY : 0);
    return input;
}

auto ButtonFlag(uint16_t button, bool down) -> DWORD
{
    switch (button)
    {
    case kMouseRight: return down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    case kMouseMiddle: return down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    default: return down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    }
}

auto Send(std::vector<INPUT>& inputs, std::string_view context) -> void
{
    if (inputs.empty())
    {
        return;
    }
    const UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    if (sent != inputs.size())
    {
        LOG_ERROR("{}SendInput delivered {} of {} events (error {})", context, sent, inputs.size(), GetLastError());
    }
}

} // namespace

auto SendInputProvider::SetModifiers(uint8_t modifiers) -> void
{
    const uint8_t changed = m_current_modifiers ^ modifiers;
    for (int bit = 0; bit < 8; ++bit)
    {
        const auto mask = static_cast<uint8_t>(1U << bit);
        if ((changed & mask) == 0)
        {
            continue;
        }
        if (const auto key = VirtualKeys::KeyForModifier(mask))
        {
            SendKey(*key, (modifiers & mask) != 0);
        }
    }
    m_current_modifiers = modifiers;
}

auto SendInputProvider::SendKey(VirtualKey key, bool down) -> void
{
    LOG_TRACE("send_key: {} {}", VirtualKeys::Name(key), down);
    auto input = KeyInput(key, down);
    if (!input)
    {
        LOG_DEBUG("send_key: {} has no Windows scancode", VirtualKeys::Name(key));
        return;
    }
    std::vector<INPUT> inputs{*input};
    Send(inputs, "send_key: ");
}

auto SendInputProvider::SetDesktopExtent(const Eigen::Vector2f& extent) -> void
{
    m_desktop_extent = extent.cwiseMax(Eigen::Vector2f::Ones());
}

auto SendInputProvider::SetDesktopOrigin(const Eigen::Vector2f& origin) -> void
{
    m_desktop_origin = origin;
}

auto SendInputProvider::MouseMove(const Eigen::Vector2f& position) -> void
{
    if (!m_pending.position && !m_pending.scroll)
    {
        m_pending.position = position;
    }
    m_pending.last_requested_position = position;
}

auto SendInputProvider::SendButton(uint16_t button, bool down) -> void
{
    if (m_pending.buttons.empty())
    {
        m_pending.position = m_pending.last_requested_position;
    }
    m_pending.buttons.push_back({.button = button, .down = down});
}

auto SendInputProvider::Wheel(int32_t delta_y, int32_t delta_x) -> void
{
    if (!m_pending.scroll)
    {
        m_pending.scroll = Eigen::Vector2i(delta_x, delta_y);
        m_pending.position.reset();
    }
}

auto SendInputProvider::Commit() -> void
{
    std::vector<INPUT> inputs;
    if (const auto position = std::exchange(m_pending.position, std::nullopt))
    {
        LOG_TRACE("Mouse move: {} {}", position->x(), position->y());
        const Eigen::Vector2f span = (m_desktop_extent - Eigen::Vector2f::Ones()).cwiseMax(Eigen::Vector2f::Ones());
        const Eigen::Vector2f scaled = ((*position - m_desktop_origin).cwiseQuotient(span) * kAbsoluteExtent)
                                           .cwiseMax(0.0f)
                                           .cwiseMin(kAbsoluteExtent);
        INPUT move{.type = INPUT_MOUSE};
        move.mi.dx = std::lround(scaled.x());
        move.mi.dy = std::lround(scaled.y());
        move.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        inputs.push_back(move);
    }
    for (const auto& button : std::exchange(m_pending.buttons, {}))
    {
        INPUT click{.type = INPUT_MOUSE};
        click.mi.dwFlags = ButtonFlag(button.button, button.down);
        inputs.push_back(click);
    }
    if (const auto scroll = std::exchange(m_pending.scroll, std::nullopt))
    {
        if (scroll->y() != 0)
        {
            INPUT wheel{.type = INPUT_MOUSE};
            wheel.mi.mouseData = static_cast<DWORD>(scroll->y());
            wheel.mi.dwFlags = MOUSEEVENTF_WHEEL;
            inputs.push_back(wheel);
        }
        if (scroll->x() != 0)
        {
            INPUT wheel{.type = INPUT_MOUSE};
            wheel.mi.mouseData = static_cast<DWORD>(scroll->x());
            wheel.mi.dwFlags = MOUSEEVENTF_HWHEEL;
            inputs.push_back(wheel);
        }
    }
    Send(inputs, "");
}
