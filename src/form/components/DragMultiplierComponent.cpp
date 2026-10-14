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

#include "DragMultiplierComponent.hpp"

#include <format>

#include "Config.hpp"
#include "document/FormParser.hpp"

auto DragMultiplierComponent::Parse(const nlohmann::json& json) -> DragMultiplierElement
{
    return {
        .font_size = json.at("font_size").get<int>(),
        .fg_color = json.at("fg_color").get<std::string>(),
        .centered = FormParser::Optional<bool>(json, "centered").value_or(false),
    };
}

auto DragMultiplierComponent::Serialize(nlohmann::ordered_json& json, const DragMultiplierElement& element) -> void
{
    json["font_size"] = element.font_size;
    json["fg_color"] = element.fg_color;
    if (element.centered)
    {
        json["centered"] = true;
    }
}

auto DragMultiplierComponent::Create(FormComponent& parent, const DragMultiplierElement& element, const FormContext& context)
    -> DragMultiplierComponent&
{
    auto& component = parent.AddComponent<DragMultiplierComponent>("");
    component.SetRect(FormParser::ToRect(element.rect));
    component.Label().SetFontSize(element.font_size);
    component.Label().SetFgColor(FormParser::Color(element.fg_color, context.theme));
    component.Label().SetCentered(element.centered);
    return component;
}

DragMultiplierComponent::DragMultiplierComponent(std::string name)
    : FormComponent(std::move(name)),
      m_label(AddComponent<LabelComponent>("label"))
{
}

auto DragMultiplierComponent::Layout() -> void
{
    m_label.SetSize(Size());
}

auto DragMultiplierComponent::Update() -> void
{
    m_label.SetText(std::format("{:.1f}", g_config->space_drag_multiplier));
}
