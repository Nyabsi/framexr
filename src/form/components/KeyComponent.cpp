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

#include "KeyComponent.hpp"

#include <algorithm>
#include <cmath>

namespace
{

constexpr float kDimmedLegend = 0.4f;

} // namespace

KeyComponent::KeyComponent(std::string name) : FormComponent(std::move(name))
{
}

auto KeyComponent::SetBgColor(const Eigen::Vector4f& bg_color) -> void
{
    m_bg_color = bg_color;
    MarkDirty();
}

auto KeyComponent::SetFgColor(const Eigen::Vector4f& fg_color) -> void
{
    m_fg_color = fg_color;
    MarkDirty();
}

auto KeyComponent::SetFontSize(int font_size) -> void
{
    m_font_size = font_size;
    MarkDirty();
}

auto KeyComponent::SetCornerRadius(std::optional<float> corner_radius) -> void
{
    m_corner_radius = corner_radius;
    MarkDirty();
}

auto KeyComponent::SetInteractive(bool interactive) -> void
{
    m_interactive = interactive;
    MarkDirty();
}

auto KeyComponent::SetLegends(KeyCapType cap_type, std::span<const std::string> labels, std::span<const std::string> ime) -> void
{
    Clear();
    m_legends.clear();
    const float w = Size().x();
    const float h = Size().y();
    const auto size = static_cast<float>(m_font_size);
    std::vector<KeyCapLegend> layout;
    switch (cap_type)
    {
    case KeyCapType::Regular: layout.push_back({true, {0.0f, 0.0f, w, h - size}}); break;
    case KeyCapType::RegularAltGr:
        layout.push_back({false, {12.0f, size + 12.0f, w, h}});
        layout.push_back({false, {std::fma(w, 0.5f, 12.0f), h - size + 8.0f, w, h}});
        break;
    case KeyCapType::Reversed:
        layout.push_back({true, {0.0f, std::fma(h, 0.5f, 2.0f), w, h * 0.5f}});
        layout.push_back({true, {0.0f, 2.0f, w, h * 0.5f}});
        break;
    case KeyCapType::ReversedAltGr:
        layout.push_back({false, {12.0f, h - size + 4.0f, w, h}});
        layout.push_back({false, {12.0f, size + 8.0f, w, h}});
        layout.push_back({false, {std::fma(w, 0.5f, 8.0f), h - size + 4.0f, w, h}});
        break;
    }
    const size_t typed = layout.size();
    if (!ime.empty())
    {
        for (auto& legend : layout)
        {
            legend.rect.w = legend.centered ? w * 0.5f : legend.rect.w;
        }
        if (ime.size() == 1)
        {
            layout.push_back({true, {w * 0.5f, 0.0f, w * 0.5f, h - size}});
        }
        else
        {
            layout.push_back({true, {w * 0.5f, std::fma(h, 0.5f, 2.0f), w * 0.5f, h * 0.5f}});
            layout.push_back({true, {w * 0.5f, 2.0f, w * 0.5f, h * 0.5f}});
        }
    }

    m_cap_type = cap_type;
    m_typed = std::min(typed, labels.size());
    for (size_t i = 0; i < layout.size(); ++i)
    {
        if (i < typed && i >= labels.size())
        {
            continue;
        }
        auto& legend = AddComponent<LabelComponent>("", i < typed ? labels[i] : ime[i - typed]);
        legend.SetRect(layout[i].rect);
        legend.SetFontSize(m_font_size);
        legend.SetCentered(layout[i].centered);
        m_legends.push_back(&legend);
    }
    Emphasize({});
}

auto KeyComponent::Emphasize(const KeyEmphasis& emphasis) -> void
{
    const size_t native = m_legends.size() - m_typed;
    size_t active = 0;
    if (emphasis.native && native > 0)
    {
        active = m_typed + (emphasis.shifted && native > 1 ? 1 : 0);
    }
    else if (emphasis.altgr && m_cap_type == KeyCapType::ReversedAltGr)
    {
        active = 2;
    }
    else if (
        (emphasis.altgr && m_cap_type == KeyCapType::RegularAltGr)
        || (emphasis.shifted && (m_cap_type == KeyCapType::Reversed || m_cap_type == KeyCapType::ReversedAltGr))
    )
    {
        active = 1;
    }
    active = active < m_legends.size() ? active : 0;
    for (size_t i = 0; i < m_legends.size(); ++i)
    {
        m_legends[i]->SetFgColor(m_fg_color * (i == active || m_legends.size() == 1 ? 1.0f : kDimmedLegend));
    }
    MarkDirty();
}

auto KeyComponent::Emit(FormState& state, const FormFrame& frame) -> void
{
    const float radius = FormTheme::Resolve(m_corner_radius, frame.rounding, Shape::Key) * frame.scale;
    state.push_back(FillItem{.rect = frame.rect, .radius = radius, .color = m_bg_color, .owner = this});
    if (m_interactive)
    {
        state.push_back(InteractItem{.rect = frame.rect, .radius = radius, .owner = this});
    }
}
