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

#include "PanelComponent.hpp"

#include "document/FormParser.hpp"

auto PanelComponent::Parse(const nlohmann::json& json) -> PanelElement {
    return {
        .corner_radius = FormParser::Optional<float>(json, "corner_radius"),
        .bg_color = json.at("bg_color").get<std::string>(),
    };
}

auto PanelComponent::Serialize(nlohmann::ordered_json& json, const PanelElement& element) -> void {
    FormParser::PutRadius(json, element.corner_radius);
    json["bg_color"] = element.bg_color;
}

auto PanelComponent::Create(FormComponent& parent, const PanelElement& element, const FormContext& context) -> PanelComponent& {
    auto& panel = parent.AddComponent<PanelComponent>();
    panel.SetRect(FormParser::ToRect(element.rect));
    panel.SetCornerRadius(element.corner_radius);
    panel.SetBgColor(FormParser::Color(element.bg_color, context.theme));
    return panel;
}

PanelComponent::PanelComponent(std::string name) : FormComponent(std::move(name)) {
}

auto PanelComponent::SetBgColor(const Eigen::Vector4f& bg_color) -> void {
    m_bg_color = bg_color;
    MarkDirty();
}

auto PanelComponent::SetCornerRadius(std::optional<float> corner_radius) -> void {
    m_corner_radius = corner_radius;
    MarkDirty();
}

auto PanelComponent::SetThemeShape(Shape shape) -> void {
    m_shape = shape;
    MarkDirty();
}

auto PanelComponent::Emit(FormState& state, const FormFrame& frame) -> void {
    state.push_back(
        FillItem{
            .rect = frame.rect,
            .radius = FormTheme::Resolve(m_corner_radius, frame.rounding, m_shape) * frame.scale,
            .color = m_bg_color,
            .owner = this,
        }
    );
}
