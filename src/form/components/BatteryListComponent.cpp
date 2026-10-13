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

#include "BatteryListComponent.hpp"

#include <algorithm>

#include "BatteryComponent.hpp"
#include "Json.hpp"
#include "document/FormParser.hpp"

namespace {

constexpr float kInset = 2.0f;

} // namespace

auto BatteryListComponent::Parse(const nlohmann::json& json) -> BatteryListElement {
    return {
        .corner_radius = FormParser::Optional<float>(json, "corner_radius"),
        .font_size = json.at("font_size").get<int>(),
        .fg_color = json.at("fg_color").get<std::string>(),
        .fg_color_low = json.at("fg_color_low").get<std::string>(),
        .fg_color_charging = json.at("fg_color_charging").get<std::string>(),
        .low_threshold = json.at("low_threshold").get<float>(),
        .num_devices = json.at("num_devices").get<size_t>(),
        .layout = FormParser::Layout(json),
    };
}

auto BatteryListComponent::Serialize(nlohmann::ordered_json& json, const BatteryListElement& element) -> void {
    FormParser::PutRadius(json, element.corner_radius);
    json["font_size"] = element.font_size;
    json["fg_color"] = element.fg_color;
    json["fg_color_low"] = element.fg_color_low;
    json["fg_color_charging"] = element.fg_color_charging;
    json["num_devices"] = element.num_devices;
    FormParser::PutLayout(json, element.layout);
    json["low_threshold"] = Json::ShortestFloat(element.low_threshold);
}

auto BatteryListComponent::Create(FormComponent& parent, const BatteryListElement& element, const FormContext& context)
    -> BatteryListComponent& {
    auto& list = parent.AddComponent<BatteryListComponent>();
    list.SetRect(FormParser::ToRect(element.rect));
    const auto count = static_cast<float>(std::max<size_t>(element.num_devices, 1));
    const float cell_w = element.layout == ListLayout::Horizontal ? element.rect[2] / count : element.rect[2];
    const float cell_h = element.layout == ListLayout::Horizontal ? element.rect[3] : element.rect[3] / count;
    const auto normal = FormParser::Color(element.fg_color, context.theme);
    const auto low = FormParser::Color(element.fg_color_low, context.theme);
    const auto charging = FormParser::Color(element.fg_color_charging, context.theme);
    float x = 0.0f;
    float y = 0.0f;
    for (size_t device = 0; device < element.num_devices; ++device) {
        auto& battery = list.AddComponent<BatteryComponent>("", device, element.low_threshold * 0.01f);
        battery.SetRect({x + kInset, y + kInset, cell_w - 2.0f * kInset, cell_h - 2.0f * kInset});
        battery.SetColors(normal, low, charging);
        battery.Label().SetFontSize(element.font_size);
        battery.Label().SetCentered(true);
        if (element.layout == ListLayout::Horizontal) {
            x += cell_w;
        } else {
            y += cell_h;
        }
    }
    return list;
}

BatteryListComponent::BatteryListComponent(std::string name) : FormComponent(std::move(name)) {
}
