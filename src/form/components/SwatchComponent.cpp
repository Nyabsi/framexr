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

#include "SwatchComponent.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "FontCache.hpp"
#include "FormTheme.hpp"
#include "OutlineComponent.hpp"

namespace {

auto RimColor() -> Eigen::Vector4f {
    auto color = FormTheme::Rgba("@subtle");
    color.w() = 0.35f;
    return color;
}

} // namespace

SwatchComponent::SwatchComponent(std::string name, std::string_view text, const Eigen::Vector4f& color, bool interactive)
    : FormComponent(std::move(name)),
      m_text(text),
      m_color(color),
      m_interactive(interactive),
      m_rim(AddComponent<OutlineComponent>("rim", std::nullopt, HighlightOutline{.color = RimColor(), .width = 1.0f})) {
}

auto SwatchComponent::Layout() -> void {
    m_rim.SetSize(Size());
}

auto SwatchComponent::Emit(FormState& state, const FormFrame& frame) -> void {
    const float radius = FormTheme::Resolve(std::nullopt, frame.rounding, Shape::Control) * frame.scale;
    state.push_back(FillItem{.rect = frame.rect, .radius = radius, .color = m_color, .owner = this});
    state.push_back(
        TextItem{
            .rect = frame.rect,
            .text = FontCache::Ellipsize(m_text, kFontSize, std::max(Size().x() - 12.0f, 8.0f)),
            .font_size = static_cast<int>(std::lround(static_cast<float>(kFontSize) * frame.scale)),
            .color = FormTheme::Readable(m_color),
            .centered = true,
            .owner = this,
        }
    );
    if (m_interactive) {
        state.push_back(InteractItem{.rect = frame.rect, .radius = radius, .owner = this});
    }
}
