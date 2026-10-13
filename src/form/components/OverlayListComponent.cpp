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

#include "OverlayListComponent.hpp"

#include <algorithm>
#include <vector>

#include "ButtonComponent.hpp"
#include "Container.hpp"
#include "Desktop.hpp"
#include "document/FormParser.hpp"

namespace {

constexpr float kInset = 2.0f;

auto ScreenActions(const std::optional<OverlayAction>& action, OverlayId id) -> std::optional<std::vector<ButtonAction>> {
    if (!action) {
        return std::nullopt;
    }
    return std::vector<ButtonAction>{OverlayButtonAction{.target = id, .action = *action}};
}

auto Put(nlohmann::ordered_json& json, std::string_view key, const std::optional<OverlayAction>& action) -> void {
    if (action) {
        json[key] = *action;
    }
}

} // namespace

auto OverlayListComponent::Parse(const nlohmann::json& json) -> OverlayListElement {
    const auto action = [&json](std::string_view key) -> std::optional<OverlayAction> {
        return FormParser::Optional<OverlayAction>(json, key);
    };
    return {
        .corner_radius = FormParser::Optional<float>(json, "corner_radius"),
        .font_size = json.at("font_size").get<int>(),
        .fg_color = json.at("fg_color").get<std::string>(),
        .bg_color = json.at("bg_color").get<std::string>(),
        .layout = FormParser::Layout(json),
        .overlay_template = {
            .click_down = action("click_down"),
            .click_up = action("click_up"),
            .long_click_up = action("long_click_up"),
            .right_down = action("right_down"),
            .right_up = action("right_up"),
            .long_right_up = action("long_right_up"),
            .middle_down = action("middle_down"),
            .middle_up = action("middle_up"),
            .long_middle_up = action("long_middle_up"),
            .scroll_down = action("scroll_down"),
            .scroll_up = action("scroll_up"),
        },
    };
}

auto OverlayListComponent::Serialize(nlohmann::ordered_json& json, const OverlayListElement& element) -> void {
    FormParser::PutRadius(json, element.corner_radius);
    json["font_size"] = element.font_size;
    json["fg_color"] = element.fg_color;
    json["bg_color"] = element.bg_color;
    FormParser::PutLayout(json, element.layout);
    const auto& actions = element.overlay_template;
    Put(json, "click_down", actions.click_down);
    Put(json, "click_up", actions.click_up);
    Put(json, "long_click_up", actions.long_click_up);
    Put(json, "right_down", actions.right_down);
    Put(json, "right_up", actions.right_up);
    Put(json, "long_right_up", actions.long_right_up);
    Put(json, "middle_down", actions.middle_down);
    Put(json, "middle_up", actions.middle_up);
    Put(json, "long_middle_up", actions.long_middle_up);
    Put(json, "scroll_down", actions.scroll_down);
    Put(json, "scroll_up", actions.scroll_up);
}

auto OverlayListComponent::Create(FormComponent& parent, const OverlayListElement& element, const FormContext& context)
    -> OverlayListComponent& {
    auto& list = parent.AddComponent<OverlayListComponent>();
    list.SetRect(FormParser::ToRect(element.rect));
    const auto count = static_cast<float>(std::max<size_t>(g_container->Desktops().size(), 1));
    const float cell_w = element.layout == ListLayout::Horizontal ? element.rect[2] / count : element.rect[2];
    const float cell_h = element.layout == ListLayout::Horizontal ? element.rect[3] : element.rect[3] / count;
    const auto bg_color = FormParser::Color(element.bg_color, context.theme);
    const auto fg_color = FormParser::Color(element.fg_color, context.theme);
    const auto& actions = element.overlay_template;
    float x = 0.0f;
    float y = 0.0f;
    for (const auto* screen : g_container->Desktops()) {
        const ButtonData data{
            .click_down = ScreenActions(actions.click_down, screen->Id()),
            .click_up = ScreenActions(actions.click_up, screen->Id()),
            .long_click_up = ScreenActions(actions.long_click_up, screen->Id()),
            .right_down = ScreenActions(actions.right_down, screen->Id()),
            .right_up = ScreenActions(actions.right_up, screen->Id()),
            .long_right_up = ScreenActions(actions.long_right_up, screen->Id()),
            .middle_down = ScreenActions(actions.middle_down, screen->Id()),
            .middle_up = ScreenActions(actions.middle_up, screen->Id()),
            .long_middle_up = ScreenActions(actions.long_middle_up, screen->Id()),
            .scroll_down = ScreenActions(actions.scroll_down, screen->Id()),
            .scroll_up = ScreenActions(actions.scroll_up, screen->Id()),
        };
        auto& button = list.AddComponent<ButtonComponent>("", screen->Name());
        button.SetRect({x + kInset, y + kInset, cell_w - 2.0f * kInset, cell_h - 2.0f * kInset});
        button.SetCornerRadius(element.corner_radius);
        button.SetFontSize(element.font_size);
        button.SetFgColor(fg_color);
        button.SetBgColor(bg_color);
        if (context.bind) {
            context.bind(button, data);
        }
        if (element.layout == ListLayout::Horizontal) {
            x += cell_w;
        } else {
            y += cell_h;
        }
    }
    return list;
}

OverlayListComponent::OverlayListComponent(std::string name) : FormComponent(std::move(name)) {
}
