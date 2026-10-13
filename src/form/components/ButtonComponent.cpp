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

#include "ButtonComponent.hpp"

#include <algorithm>
#include <cmath>

#include "FontCache.hpp"
#include "document/FormParser.hpp"

auto ButtonComponent::Parse(const nlohmann::json& json) -> ButtonElement {
    return {
        .corner_radius = FormParser::Optional<float>(json, "corner_radius"),
        .font_size = json.at("font_size").get<int>(),
        .fg_color = json.at("fg_color").get<std::string>(),
        .bg_color = json.at("bg_color").get<std::string>(),
        .text = json.at("text").get<std::string>(),
        .data = json.get<ButtonData>(),
    };
}

auto ButtonComponent::Serialize(nlohmann::ordered_json& json, const ButtonElement& element) -> void {
    FormParser::PutRadius(json, element.corner_radius);
    json["font_size"] = element.font_size;
    json["fg_color"] = element.fg_color;
    json["bg_color"] = element.bg_color;
    json["text"] = element.text;
    FormParser::Merge(json, nlohmann::ordered_json(element.data));
}

auto ButtonComponent::Create(FormComponent& parent, const ButtonElement& element, const FormContext& context) -> ButtonComponent& {
    auto& button = parent.AddComponent<ButtonComponent>("", element.text);
    button.SetRect(FormParser::ToRect(element.rect));
    button.SetCornerRadius(element.corner_radius);
    button.SetFontSize(element.font_size);
    button.SetFgColor(FormParser::Color(element.fg_color, context.theme));
    button.SetBgColor(FormParser::Color(element.bg_color, context.theme));
    if (context.bind) {
        context.bind(button, element.data);
    }
    return button;
}

ButtonComponent::ButtonComponent(std::string name, std::string_view text) : FormComponent(std::move(name)), m_text(text) {
}

auto ButtonComponent::SetText(std::string_view text) -> void {
    if (m_text != text) {
        m_text = text;
        MarkDirty();
    }
}

auto ButtonComponent::SetFontSize(int font_size) -> void {
    m_font_size = font_size;
    MarkDirty();
}

auto ButtonComponent::SetFgColor(const Eigen::Vector4f& fg_color) -> void {
    m_fg_color = fg_color;
    MarkDirty();
}

auto ButtonComponent::SetBgColor(const Eigen::Vector4f& bg_color) -> void {
    m_bg_color = bg_color;
    MarkDirty();
}

auto ButtonComponent::SetCornerRadius(std::optional<float> corner_radius) -> void {
    m_corner_radius = corner_radius;
    MarkDirty();
}

auto ButtonComponent::SetEllipsize(bool ellipsize) -> void {
    m_ellipsize = ellipsize;
    MarkDirty();
}

auto ButtonComponent::Emit(FormState& state, const FormFrame& frame) -> void {
    const float radius = FormTheme::Resolve(m_corner_radius, frame.rounding, Shape::Control) * frame.scale;
    state.push_back(FillItem{.rect = frame.rect, .radius = radius, .color = m_bg_color, .owner = this});
    state.push_back(
        TextItem{
            .rect = frame.rect,
            .text = m_ellipsize ? FontCache::Ellipsize(m_text, m_font_size, std::max(Size().x() - 12.0f, 8.0f)) : m_text,
            .font_size = static_cast<int>(std::lround(static_cast<float>(m_font_size) * frame.scale)),
            .color = m_fg_color,
            .centered = true,
            .owner = this,
        }
    );
    state.push_back(InteractItem{.rect = frame.rect, .radius = radius, .owner = this});
}
