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

#include "TimezoneComponent.hpp"

#include "ClockComponent.hpp"
#include "document/FormParser.hpp"

auto TimezoneComponent::Parse(const nlohmann::json& json) -> TimezoneElement {
    return {
        .font_size = json.at("font_size").get<int>(),
        .fg_color = json.at("fg_color").get<std::string>(),
        .centered = FormParser::Optional<bool>(json, "centered").value_or(false),
        .timezone = FormParser::Timezone(json).value_or(size_t{0}),
    };
}

auto TimezoneComponent::Serialize(nlohmann::ordered_json& json, const TimezoneElement& element) -> void {
    json["font_size"] = element.font_size;
    json["fg_color"] = element.fg_color;
    if (element.centered) {
        json["centered"] = true;
    }
    FormParser::PutTimezone(json, element.timezone);
}

auto TimezoneComponent::Create(FormComponent& parent, const TimezoneElement& element, const FormContext& context)
    -> TimezoneComponent& {
    auto& component = parent.AddComponent<TimezoneComponent>("");
    component.SetRect(FormParser::ToRect(element.rect));
    component.Label().SetFontSize(element.font_size);
    component.Label().SetFgColor(FormParser::Color(element.fg_color, context.theme));
    component.Label().SetCentered(element.centered);
    if (const auto name = ClockComponent::ZoneName(element.timezone)) {
        component.Label().SetText(ClockComponent::City(*name));
    } else {
        component.Label().SetText("Error");
        component.Label().SetFgColor(FormTheme::FallbackColor());
    }
    return component;
}

TimezoneComponent::TimezoneComponent(std::string name, std::string_view zone)
    : FormComponent(std::move(name)),
      m_label(AddComponent<LabelComponent>("label", zone)) {
}

auto TimezoneComponent::Layout() -> void {
    m_label.SetSize(Size());
}
