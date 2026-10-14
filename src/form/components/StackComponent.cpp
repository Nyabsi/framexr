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

#include "StackComponent.hpp"

#include <utility>

StackComponent::StackComponent(std::string name, float padding, float gap)
    : FormComponent(std::move(name)),
      m_padding(padding),
      m_gap(gap)
{
}

auto StackComponent::Bottom() const -> float
{
    float y = m_padding;
    for (const auto& child : Children())
    {
        y += child->Size().y() + m_gap;
    }
    return y;
}

auto StackComponent::SetBgColor(const Eigen::Vector4f& color) -> void
{
    m_background = color;
    MarkDirty();
}

auto StackComponent::Emit(FormState& state, const FormFrame& frame) -> void
{
    if (m_background)
    {
        const float radius = FormTheme::Resolve(std::nullopt, frame.rounding, Shape::Panel) * frame.scale;
        state.push_back(FillItem{.rect = frame.rect, .radius = radius, .color = *m_background, .owner = this});
    }
}

auto StackComponent::Layout() -> void
{
    const float width = Size().x() - 2.0f * m_padding;
    float y = m_padding;
    for (const auto& child : Children())
    {
        child->SetRect({m_padding, y, width, child->Size().y()});
        y += child->Size().y() + m_gap;
    }
}
