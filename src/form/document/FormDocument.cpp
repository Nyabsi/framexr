// FrameXR
//
// This is a port of wlx-overlay-s from Rust to C++.
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
// Copyright (C) 2023-2025 galister and wlx-overlay-s contributors
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

#include "document/FormDocument.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

#include "document/FormElements.hpp"
#include "document/FormParser.hpp"
#include "framexr_config.h"

namespace {

constexpr float kMinToolbarGap = 2.0f;

constexpr std::array<std::pair<ToolbarPosition, std::string_view>, 2> kToolbarPositions{{
    {ToolbarPosition::Above, "Above"},
    {ToolbarPosition::Below, "Below"},
}};

auto OptionalColor(const nlohmann::json& json) -> std::optional<std::string> {
    const auto it = json.find("bg_color");
    return it == json.end() || it->is_null() ? std::nullopt : std::optional(it->get<std::string>());
}

} // namespace

auto FormDocument::Size() const -> std::array<uint32_t, 2> {
    const Eigen::Vector2f origin = Origin();
    float right = origin.x() + m_form.width;
    float bottom = origin.y() + m_form.height;
    for (size_t index = 1; index < FormCount(); ++index) {
        const auto rect = FormRect(index, origin);
        right = std::max(right, rect.x + rect.w);
        bottom = std::max(bottom, rect.y + rect.h);
    }
    if (const auto toolbar = ToolbarRect(origin)) {
        right = std::max(right, toolbar->x + toolbar->w);
        bottom = std::max(bottom, toolbar->y + toolbar->h);
    }
    return {static_cast<uint32_t>(std::ceil(right)), static_cast<uint32_t>(std::ceil(bottom))};
}

auto FormDocument::Origin() const -> Eigen::Vector2f {
    Eigen::Vector2f minimum = Eigen::Vector2f::Zero();
    for (const auto& extra : m_forms) {
        minimum = minimum.cwiseMin(Eigen::Vector2f(extra.x, extra.y));
    }
    if (m_form.toolbar && m_form.toolbar->position == ToolbarPosition::Above) {
        minimum.y() = std::min(minimum.y(), -(m_form.toolbar->height + m_form.toolbar->gap));
    }
    return -minimum;
}

auto FormDocument::FormRect(size_t index) const -> CanvasRect {
    return FormRect(index, Origin());
}

auto FormDocument::FormRect(size_t index, const Eigen::Vector2f& origin) const -> CanvasRect {
    if (index == 0 || index > m_forms.size()) {
        return {origin.x(), origin.y(), m_form.width, m_form.height};
    }
    const auto& extra = m_forms[index - 1];
    return {origin.x() + extra.x, origin.y() + extra.y, extra.width, extra.height};
}

auto FormDocument::ToolbarRect() const -> std::optional<CanvasRect> {
    return ToolbarRect(Origin());
}

auto FormDocument::ToolbarRect(const Eigen::Vector2f& origin) const -> std::optional<CanvasRect> {
    if (!m_form.toolbar) {
        return std::nullopt;
    }
    const auto& toolbar = *m_form.toolbar;
    const float top = toolbar.position == ToolbarPosition::Above ? -(toolbar.height + toolbar.gap) : m_form.height + toolbar.gap;
    return CanvasRect{origin.x(), origin.y() + top, m_form.width, toolbar.height};
}

auto FormDocument::Backgrounds(const Eigen::Vector2f& origin) const -> std::vector<FormElement> {
    std::vector<FormElement> backgrounds;
    for (size_t index = 0; index < FormCount(); ++index) {
        const auto& color = index == 0 ? m_form.bg_color : m_forms[index - 1].bg_color;
        if (color) {
            const auto rect = FormRect(index, origin);
            backgrounds.emplace_back(PanelElement{.rect = {rect.x, rect.y, rect.w, rect.h}, .bg_color = *color});
        }
    }
    return backgrounds;
}

auto FormDocument::RemoveForm(size_t index) -> void {
    if (index == 0 || index > m_forms.size()) {
        return;
    }
    m_forms.erase(m_forms.begin() + static_cast<std::ptrdiff_t>(index) - 1);
    std::erase_if(m_components, [index](const FormElement& element) -> bool {
        return FormElements::RegionOf(element) == Region::Form && FormElements::FormOf(element) == index;
    });
    for (auto& element : m_components) {
        if (const auto form_index = FormElements::FormOf(element); form_index > index) {
            FormElements::SetForm(element, form_index - 1);
        }
    }
}

auto FormDocument::Arrange() const -> std::vector<FormElement> {
    return Arrange(Origin());
}

auto FormDocument::Arrange(const Eigen::Vector2f& origin) const -> std::vector<FormElement> {
    const auto toolbar_rect = ToolbarRect(origin);
    std::vector<FormElement> arranged;
    std::vector<size_t> toolbar_items;
    for (const auto& element : m_components) {
        auto& placed = arranged.emplace_back(element);
        auto& rect = FormElements::Rect(placed);
        if (FormElements::RegionOf(element) == Region::Toolbar) {
            toolbar_items.push_back(arranged.size() - 1);
        } else {
            const auto form_rect = FormRect(FormElements::FormOf(element), origin);
            rect[0] += form_rect.x;
            rect[1] += form_rect.y;
        }
    }
    if (toolbar_items.empty()) {
        return arranged;
    }
    if (!toolbar_rect) {
        for (const auto index : std::views::reverse(toolbar_items)) {
            arranged.erase(arranged.begin() + static_cast<std::ptrdiff_t>(index));
        }
        return arranged;
    }

    const auto count = static_cast<float>(toolbar_items.size());
    float total = 0.0f;
    for (const auto index : toolbar_items) {
        total += FormElements::Rect(arranged[index])[2];
    }
    float gap = (toolbar_rect->w - total) / (count + 1.0f);
    float stretch = 1.0f;
    if (gap < kMinToolbarGap && total > 0.0f) {
        gap = kMinToolbarGap;
        stretch = std::max(toolbar_rect->w - gap * (count + 1.0f), 0.0f) / total;
    }
    const float padding = m_form.toolbar ? m_form.toolbar->padding : 0.0f;
    float x = toolbar_rect->x + gap;
    for (const auto index : toolbar_items) {
        auto& rect = FormElements::Rect(arranged[index]);
        const float item_width = rect[2] * stretch;
        if (FormElements::IsList(arranged[index])) {
            rect = {x, toolbar_rect->y, item_width, toolbar_rect->h};
        } else {
            rect = {x, toolbar_rect->y + padding, item_width, std::max(toolbar_rect->h - 2.0f * padding, 1.0f)};
        }
        x += item_width + gap;
    }
    return arranged;
}

auto from_json(const nlohmann::json& json, FormDocument& value) -> void {
    value.SetWidth(json.at("width").get<float>());
    if (const auto it = json.find("theme"); it != json.end() && !it->is_null()) {
        value.SetTheme(it->get<std::string>());
    }
    if (const auto it = json.find("spawn_pos"); it != json.end() && !it->is_null()) {
        value.SpawnPos() = it->get<std::array<float, 3>>();
    }
    const auto& form = json.at("form");
    value.SetForm(
        FormLayout{
            .width = form.at("width").get<float>(),
            .height = form.at("height").get<float>(),
            .bg_color = OptionalColor(form),
        }
    );
    if (const auto extras = json.find("forms"); extras != json.end() && !extras->is_null()) {
        for (const auto& extra : *extras) {
            const auto at = extra.at("at").get<std::array<float, 2>>();
            value.Forms().push_back({
                .x = at[0],
                .y = at[1],
                .width = extra.at("width").get<float>(),
                .height = extra.at("height").get<float>(),
                .bg_color = OptionalColor(extra),
            });
        }
    }
    if (const auto toolbar = form.find("toolbar"); toolbar != form.end() && !toolbar->is_null()) {
        ToolbarLayout layout{.height = toolbar->at("height").get<float>()};
        if (const auto gap = toolbar->find("gap"); gap != toolbar->end()) {
            layout.gap = gap->get<float>();
        }
        if (const auto padding = toolbar->find("padding"); padding != toolbar->end()) {
            layout.padding = padding->get<float>();
        }
        if (const auto position = toolbar->find("position"); position != toolbar->end()) {
            layout.position = FormParser::ParseName(*position, kToolbarPositions, "ToolbarPosition");
        }
        value.Form().toolbar = layout;
    }
    value.SetComponents(json.at("components").get<std::vector<FormElement>>());
}

auto to_json(nlohmann::ordered_json& json, const FormDocument& value) -> void {
    json = nlohmann::ordered_json::object();
    json["width"] = Json::ShortestFloat(value.Width());
    if (value.Theme()) {
        json["theme"] = *value.Theme();
    }
    if (value.SpawnPos()) {
        const auto& spawn = *value.SpawnPos();
        json["spawn_pos"] = {Json::ShortestFloat(spawn[0]), Json::ShortestFloat(spawn[1]), Json::ShortestFloat(spawn[2])};
    }
    nlohmann::ordered_json form;
    form["width"] = Json::ShortestFloat(value.Form().width);
    form["height"] = Json::ShortestFloat(value.Form().height);
    if (value.Form().toolbar) {
        const auto& toolbar = *value.Form().toolbar;
        form["toolbar"] = {
            {"height", Json::ShortestFloat(toolbar.height)},
            {"gap", Json::ShortestFloat(toolbar.gap)},
            {"padding", Json::ShortestFloat(toolbar.padding)},
            {"position", FormParser::NameOf(toolbar.position, kToolbarPositions)},
        };
    }
    if (value.Form().bg_color) {
        form["bg_color"] = *value.Form().bg_color;
    }
    json["form"] = std::move(form);
    if (!value.Forms().empty()) {
        auto extras = nlohmann::ordered_json::array();
        for (const auto& extra : value.Forms()) {
            nlohmann::ordered_json item;
            item["at"] = {Json::ShortestFloat(extra.x), Json::ShortestFloat(extra.y)};
            item["width"] = Json::ShortestFloat(extra.width);
            item["height"] = Json::ShortestFloat(extra.height);
            if (extra.bg_color) {
                item["bg_color"] = *extra.bg_color;
            }
            extras.push_back(std::move(item));
        }
        json["forms"] = std::move(extras);
    }
    json["components"] = value.Components();
}
