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

#include "ColorDialog.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "ColorPickerComponent.hpp"
#include "Editor.hpp"

namespace
{

constexpr size_t kRecentColors = 8;

}

auto ColorDialog::Open(ColorTarget target) -> void
{
    m_hsv = ColorPickerComponent::Hsv(target.get());
    m_target = std::move(target);
}

auto ColorDialog::SetRects(const CanvasRect& wheel, const CanvasRect& bar) -> void
{
    m_wheel_rect = wheel;
    m_bar_rect = bar;
}

auto ColorDialog::WheelImage() -> std::shared_ptr<Image>
{
    if (!m_wheel || std::abs(m_wheel_value - m_hsv[2]) > 1e-3f)
    {
        m_wheel = ColorPickerComponent::WheelImage(m_hsv[2]);
        m_wheel_value = m_hsv[2];
    }
    return m_wheel;
}

auto ColorDialog::BarImage() -> std::shared_ptr<Image>
{
    if (const Eigen::Vector2f key(m_hsv[0], m_hsv[1]); !m_bar || (key - m_bar_key).norm() > 1e-3f)
    {
        m_bar = ColorPickerComponent::BarImage(m_hsv[0], m_hsv[1]);
        m_bar_key = key;
    }
    return m_bar;
}

auto ColorDialog::Pick(const std::string& color) -> void
{
    if (!m_target)
    {
        return;
    }
    m_target->set(color);
    m_hsv = ColorPickerComponent::Hsv(color);
    Remember(color);
}

auto ColorDialog::Remember(const std::string& color) -> void
{
    std::erase(m_recent, color);
    m_recent.push_front(color);
    if (m_recent.size() > kRecentColors)
    {
        m_recent.pop_back();
    }
}

auto ColorDialog::Drag(Editor& editor, const Eigen::Vector2f& point) -> void
{
    if (!m_target)
    {
        return;
    }
    if (m_drag == ColorDrag::Wheel)
    {
        const float radius = m_wheel_rect.w / 2.0f;
        const Eigen::Vector2f offset = point - Eigen::Vector2f(m_wheel_rect.x + radius, m_wheel_rect.y + radius);
        const float hue = std::atan2(offset.y(), offset.x()) / (2.0f * std::numbers::pi_v<float>);
        m_hsv[0] = hue < 0.0f ? hue + 1.0f : hue;
        m_hsv[1] = std::clamp(offset.norm() / radius, 0.0f, 1.0f);
    }
    else if (m_drag == ColorDrag::Brightness)
    {
        m_hsv[2] = std::clamp((point.x() - m_bar_rect.x) / m_bar_rect.w, 0.0f, 1.0f);
    }
    else
    {
        return;
    }
    m_target->set(ColorPickerComponent::Hex(m_hsv[0], m_hsv[1], m_hsv[2]));
    editor.SetAmend(true);
    editor.Touch();
}

auto ColorDialog::Pointer(Editor& editor, const Eigen::Vector2f& point, bool pressed) -> bool
{
    if (!m_target)
    {
        return false;
    }
    if (!pressed)
    {
        const bool dragging = Dragging();
        m_drag = ColorDrag::None;
        editor.SetAmend(false);
        if (dragging)
        {
            Remember(m_target->get());
            editor.Touch();
        }
        return dragging;
    }
    const float radius = m_wheel_rect.w / 2.0f;
    const Eigen::Vector2f center(m_wheel_rect.x + radius, m_wheel_rect.y + radius);
    if ((point - center).norm() <= radius + 2.0f)
    {
        m_drag = ColorDrag::Wheel;
    }
    else if (
        point.x() >= m_bar_rect.x - 4.0f && point.x() <= m_bar_rect.x + m_bar_rect.w + 4.0f && point.y() >= m_bar_rect.y - 6.0f
        && point.y() <= m_bar_rect.y + m_bar_rect.h + 6.0f
    )
    {
        m_drag = ColorDrag::Brightness;
    }
    else
    {
        return false;
    }
    editor.SetAmend(false);
    Drag(editor, point);
    return true;
}

auto ColorDialog::Hover(Editor& editor, const Eigen::Vector2f& point) -> bool
{
    if (!Dragging())
    {
        return false;
    }
    Drag(editor, point);
    return true;
}
