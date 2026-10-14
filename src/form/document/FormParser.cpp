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

#include "document/FormParser.hpp"

#include <type_traits>

#include "Logger.hpp"
#include "Yaml.hpp"
#include "document/FormElements.hpp"

namespace
{

constexpr auto kRegionNames = std::to_array<std::pair<Region, std::string_view>>({
    {Region::Form, "Form"},
    {Region::Toolbar, "Toolbar"},
});

constexpr auto kLayoutNames = std::to_array<std::pair<ListLayout, std::string_view>>({
    {ListLayout::Horizontal, "Horizontal"},
    {ListLayout::Vertical, "Vertical"},
});

auto RegionFrom(const nlohmann::json& json) -> Region
{
    const auto it = json.find("region");
    return it == json.end() || it->is_null() ? Region::Form : FormParser::ParseName(*it, kRegionNames, "Region");
}

auto RectFrom(const nlohmann::json& json, Region region) -> std::array<float, 4>
{
    if (const auto width = json.find("width"); width != json.end() && region == Region::Toolbar && !json.contains("rect"))
    {
        return {0.0f, 0.0f, width->get<float>(), 0.0f};
    }
    return json.at("rect").get<std::array<float, 4>>();
}

template <typename Element>
auto Placed(Element element, const nlohmann::json& json) -> FormElement
{
    element.region = RegionFrom(json);
    element.form = FormParser::Optional<size_t>(json, "form").value_or(0);
    element.rect = RectFrom(json, element.region);
    return element;
}

auto PutPlacement(nlohmann::ordered_json& json, const std::array<float, 4>& rect, Region region, size_t form) -> void
{
    if (region == Region::Toolbar)
    {
        json["region"] = FormParser::NameOf(region, kRegionNames);
        json["width"] = Json::ShortestFloat(rect[2]);
        return;
    }
    if (form > 0)
    {
        json["form"] = form;
    }
    json["rect"] = {
        Json::ShortestFloat(rect[0]),
        Json::ShortestFloat(rect[1]),
        Json::ShortestFloat(rect[2]),
        Json::ShortestFloat(rect[3]),
    };
}

} // namespace

auto FormParser::Parse(std::string_view yaml) -> Result<FormDocument>
{
    ASSIGN_OR_RETURN(const auto json, Yaml::Parse(yaml));
    try
    {
        return json.get<FormDocument>();
    }
    catch (const nlohmann::json::exception& error)
    {
        return Unexpected(error.what());
    }
}

auto FormParser::Emit(const FormDocument& document) -> std::string
{
    return Yaml::Emit(nlohmann::ordered_json(document));
}

auto FormParser::ToRect(const std::array<float, 4>& rect) -> CanvasRect
{
    return {rect[0], rect[1], rect[2], rect[3]};
}

auto FormParser::Color(const std::string& color, const ThemeDefinition& theme) -> Eigen::Vector4f
{
    if (auto parsed = FormTheme::ParseColor(color, theme))
    {
        return *parsed;
    }
    else
    {
        LOG_ERROR("Failed to parse color '{}': {}", color, parsed.error().Message());
        return FormTheme::FallbackColor();
    }
}

auto FormParser::Layout(const nlohmann::json& json) -> ListLayout
{
    return ParseName(json.at("layout"), kLayoutNames, "ListLayout");
}

auto FormParser::PutLayout(nlohmann::ordered_json& json, ListLayout layout) -> void
{
    json["layout"] = NameOf(layout, kLayoutNames);
}

auto FormParser::Timezone(const nlohmann::json& json) -> std::optional<std::variant<size_t, std::string>>
{
    const auto it = json.find("timezone");
    if (it == json.end() || it->is_null())
    {
        return std::nullopt;
    }
    if (it->is_number())
    {
        return it->get<size_t>();
    }
    return it->get<std::string>();
}

auto FormParser::PutTimezone(nlohmann::ordered_json& json, const std::variant<size_t, std::string>& zone) -> void
{
    if (const auto* index = std::get_if<size_t>(&zone))
    {
        json["timezone"] = *index;
    }
    else
    {
        json["timezone"] = std::get<std::string>(zone);
    }
}

auto FormParser::PutRadius(nlohmann::ordered_json& json, std::optional<float> radius) -> void
{
    if (radius)
    {
        json["corner_radius"] = Json::ShortestFloat(*radius);
    }
}

auto FormParser::Merge(nlohmann::ordered_json& json, const nlohmann::ordered_json& extra) -> void
{
    for (const auto& [key, value] : extra.items())
    {
        json[key] = value;
    }
}

auto from_json(const nlohmann::json& json, FormElement& value) -> void
{
    const auto type = json.at("type").get<std::string>();
    if (type == PanelComponent::kType)
    {
        value = Placed(PanelComponent::Parse(json), json);
    }
    else if (type == LabelComponent::kType)
    {
        value = Placed(LabelComponent::Parse(json), json);
    }
    else if (type == ClockComponent::kType)
    {
        value = Placed(ClockComponent::Parse(json), json);
    }
    else if (type == TimezoneComponent::kType)
    {
        value = Placed(TimezoneComponent::Parse(json), json);
    }
    else if (type == TimerComponent::kType)
    {
        value = Placed(TimerComponent::Parse(json), json);
    }
    else if (type == BatteryComponent::kType)
    {
        value = Placed(BatteryComponent::Parse(json), json);
    }
    else if (type == ExecComponent::kType)
    {
        value = Placed(ExecComponent::Parse(json), json);
    }
    else if (type == IpdComponent::kType)
    {
        value = Placed(IpdComponent::Parse(json), json);
    }
    else if (type == DragMultiplierComponent::kType)
    {
        value = Placed(DragMultiplierComponent::Parse(json), json);
    }
    else if (type == SpriteComponent::kType)
    {
        value = Placed(SpriteComponent::Parse(json), json);
    }
    else if (type == ButtonComponent::kType)
    {
        value = Placed(ButtonComponent::Parse(json), json);
    }
    else if (type == BatteryListComponent::kType)
    {
        value = Placed(BatteryListComponent::Parse(json), json);
    }
    else if (type == OverlayListComponent::kType)
    {
        value = Placed(OverlayListComponent::Parse(json), json);
    }
    else
    {
        throw nlohmann::json::other_error::create(501, std::format("unknown component type `{}`", type), &json);
    }
}

auto to_json(nlohmann::ordered_json& json, const FormElement& value) -> void
{
    json = nlohmann::ordered_json::object();
    json["type"] = FormElements::TypeName(value);
    PutPlacement(json, FormElements::Rect(value), FormElements::RegionOf(value), FormElements::FormOf(value));
    std::visit(
        [&json](const auto& item) -> void
        {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, PanelElement>)
            {
                PanelComponent::Serialize(json, item);
            }
            else if constexpr (std::is_same_v<Item, LabelElement>)
            {
                LabelComponent::Serialize(json, item);
            }
            else if constexpr (std::is_same_v<Item, ClockElement>)
            {
                ClockComponent::Serialize(json, item);
            }
            else if constexpr (std::is_same_v<Item, TimezoneElement>)
            {
                TimezoneComponent::Serialize(json, item);
            }
            else if constexpr (std::is_same_v<Item, TimerElement>)
            {
                TimerComponent::Serialize(json, item);
            }
            else if constexpr (std::is_same_v<Item, BatteryElement>)
            {
                BatteryComponent::Serialize(json, item);
            }
            else if constexpr (std::is_same_v<Item, ExecElement>)
            {
                ExecComponent::Serialize(json, item);
            }
            else if constexpr (std::is_same_v<Item, IpdElement>)
            {
                IpdComponent::Serialize(json, item);
            }
            else if constexpr (std::is_same_v<Item, DragMultiplierElement>)
            {
                DragMultiplierComponent::Serialize(json, item);
            }
            else if constexpr (std::is_same_v<Item, SpriteElement>)
            {
                SpriteComponent::Serialize(json, item);
            }
            else if constexpr (std::is_same_v<Item, ButtonElement>)
            {
                ButtonComponent::Serialize(json, item);
            }
            else if constexpr (std::is_same_v<Item, BatteryListElement>)
            {
                BatteryListComponent::Serialize(json, item);
            }
            else
            {
                OverlayListComponent::Serialize(json, item);
            }
        },
        value
    );
}
