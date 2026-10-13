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

#include <cmath>

KeyComponent::KeyComponent(std::string name) : FormComponent(std::move(name)) {
}

auto KeyComponent::SetBgColor(const Eigen::Vector4f& bg_color) -> void {
    m_bg_color = bg_color;
    MarkDirty();
}

auto KeyComponent::SetFgColor(const Eigen::Vector4f& fg_color) -> void {
    m_fg_color = fg_color;
    MarkDirty();
}

auto KeyComponent::SetFontSize(int font_size) -> void {
    m_font_size = font_size;
    MarkDirty();
}

auto KeyComponent::SetCornerRadius(std::optional<float> corner_radius) -> void {
    m_corner_radius = corner_radius;
    MarkDirty();
}

auto KeyComponent::SetInteractive(bool interactive) -> void {
    m_interactive = interactive;
    MarkDirty();
}

auto KeyComponent::SetLegends(KeyCapType cap_type, std::span<const std::string> labels) -> void {
    Clear();
    m_legends.clear();
    const float w = Size().x();
    const float h = Size().y();
    const auto size = static_cast<float>(m_font_size);
    std::vector<KeyCapLegend> layout;
    switch (cap_type) {
    case KeyCapType::Regular: layout.push_back({true, {0.0f, 0.0f, w, h - size}, 1.0f}); break;
    case KeyCapType::RegularAltGr:
        layout.push_back({false, {12.0f, size + 12.0f, w, h}, 1.0f});
        layout.push_back({false, {std::fma(w, 0.5f, 12.0f), h - size + 8.0f, w, h}, 0.8f});
        break;
    case KeyCapType::Reversed:
        layout.push_back({true, {0.0f, std::fma(h, 0.5f, 2.0f), w, h * 0.5f}, 1.0f});
        layout.push_back({true, {0.0f, 2.0f, w, h * 0.5f}, 0.8f});
        break;
    case KeyCapType::ReversedAltGr:
        layout.push_back({false, {12.0f, h - size + 4.0f, w, h}, 1.0f});
        layout.push_back({false, {12.0f, size + 8.0f, w, h}, 0.8f});
        layout.push_back({false, {std::fma(w, 0.5f, 8.0f), h - size + 4.0f, w, h}, 0.8f});
        break;
    }

    for (size_t i = 0; i < layout.size() && i < labels.size(); ++i) {
        auto& legend = AddComponent<LabelComponent>("", labels[i]);
        legend.SetRect(layout[i].rect);
        legend.SetFgColor(m_fg_color * layout[i].alpha);
        legend.SetFontSize(m_font_size);
        legend.SetCentered(layout[i].centered);
        m_legends.push_back(&legend);
    }
    MarkDirty();
}

auto KeyComponent::Emit(FormState& state, const FormFrame& frame) -> void {
    const float radius = FormTheme::Resolve(m_corner_radius, frame.rounding, Shape::Key) * frame.scale;
    state.push_back(FillItem{.rect = frame.rect, .radius = radius, .color = m_bg_color, .owner = this});
    if (m_interactive) {
        state.push_back(InteractItem{.rect = frame.rect, .radius = radius, .owner = this});
    }
}
