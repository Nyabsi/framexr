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

#include "OutlineComponent.hpp"

OutlineComponent::OutlineComponent(
    std::string name,
    const std::optional<Eigen::Vector4f>& fill,
    const std::optional<HighlightOutline>& outline
)
    : FormComponent(std::move(name))
{
    SetHighlightColor([fill]() -> std::optional<Eigen::Vector4f> { return fill; });
    SetOutline([outline]() -> std::optional<HighlightOutline> { return outline; });
}

auto OutlineComponent::SetCornerRadius(std::optional<float> corner_radius) -> void
{
    m_corner_radius = corner_radius;
    MarkDirty();
}

auto OutlineComponent::Emit(FormState& state, const FormFrame& frame) -> void
{
    state.push_back(
        InteractItem{
            .rect = frame.rect,
            .radius = FormTheme::Resolve(m_corner_radius, frame.rounding, Shape::Control) * frame.scale,
            .owner = this,
            .clickable = false,
        }
    );
}
