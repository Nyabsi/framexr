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

#include "TextLineComponent.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "FontCache.hpp"

TextLineComponent::TextLineComponent(
    std::string name,
    std::string_view text,
    int font_size,
    const Eigen::Vector4f& color,
    float height
)
    : FormComponent(std::move(name)),
      m_text(text),
      m_font_size(font_size),
      m_color(color)
{
    SetSize({0.0f, height});
}

auto TextLineComponent::SetCentered(bool centered) -> void
{
    m_centered = centered;
    MarkDirty();
}

auto TextLineComponent::Emit(FormState& state, const FormFrame& frame) -> void
{
    auto rect = frame.rect;
    if (!m_centered)
    {
        rect.y += static_cast<float>(m_font_size) * frame.scale;
    }
    state.push_back(
        TextItem{
            .rect = rect,
            .text = FontCache::Ellipsize(m_text, m_font_size, std::max(Size().x() - (m_centered ? 12.0f : 0.0f), 8.0f)),
            .font_size = static_cast<int>(std::lround(static_cast<float>(m_font_size) * frame.scale)),
            .color = m_color,
            .centered = m_centered,
            .owner = this,
        }
    );
}
