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

#include "Modular.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

#include "CommandBuffer.hpp"
#include "Config.hpp"
#include "Device.hpp"
#include "Json.hpp"
#include "Logger.hpp"
#include "OverlayContainer.hpp"
#include "Theme.hpp"
#include "framexr_config.h"

namespace {

constexpr float kCityGap = 6.0f;

constexpr float kCityLine = 1.3f;

constexpr std::array<std::pair<Region, std::string_view>, 2> kRegionNames{{
    {Region::Form, "Form"},
    {Region::Toolbar, "Toolbar"},
}};

constexpr std::array<std::pair<ToolbarPosition, std::string_view>, 2> kToolbarPositions{{
    {ToolbarPosition::Above, "Above"},
    {ToolbarPosition::Below, "Below"},
}};

template <typename Enum, size_t Count>
auto ParseName(const nlohmann::json& json, const std::array<std::pair<Enum, std::string_view>, Count>& names, std::string_view type)
    -> Enum {
    const auto name = Json::VariantName(json);
    for (const auto& [value, value_name] : names) {
        if (value_name == name) {
            return value;
        }
    }
    throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for {}", name, type), &json);
}

template <typename Enum, size_t Count>
auto NameOf(Enum value, const std::array<std::pair<Enum, std::string_view>, Count>& names) -> std::string_view {
    for (const auto& [item, name] : names) {
        if (item == value) {
            return name;
        }
    }
    return names.front().second;
}

auto Color(const std::string& hex) -> Eigen::Vector4f {
    return Canvas::ParseColor(hex).value_or(Canvas::FallbackColor());
}

auto RegionFrom(const nlohmann::json& json) -> Region {
    const auto it = json.find("region");
    return it == json.end() || it->is_null() ? Region::Form : ParseName(*it, kRegionNames, "Region");
}

auto FormFrom(const nlohmann::json& json) -> size_t {
    const auto it = json.find("form");
    return it == json.end() || it->is_null() ? 0 : it->get<size_t>();
}

auto OptionalColor(const nlohmann::json& json) -> std::optional<std::string> {
    const auto it = json.find("bg_color");
    return it == json.end() || it->is_null() ? std::nullopt : std::optional(it->get<std::string>());
}

auto Rect(const nlohmann::json& json) -> std::array<float, 4> {
    if (const auto width = json.find("width");
        width != json.end() && RegionFrom(json) == Region::Toolbar && !json.contains("rect")) {
        return {0.0f, 0.0f, width->get<float>(), 0.0f};
    }
    return json.at("rect").get<std::array<float, 4>>();
}

auto CornerRadius(const nlohmann::json& json) -> std::optional<float> {
    const auto it = json.find("corner_radius");
    return it == json.end() || it->is_null() ? std::nullopt : std::optional(it->get<float>());
}

auto Layout(const nlohmann::json& json) -> ListLayout {
    const auto name = Json::VariantName(json.at("layout"));
    if (name == "Horizontal") {
        return ListLayout::Horizontal;
    }
    if (name == "Vertical") {
        return ListLayout::Vertical;
    }
    throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for ListLayout", name), &json);
}

auto OptionalOverlayAction(const nlohmann::json& json, std::string_view key) -> std::optional<OverlayAction> {
    const auto it = json.find(key);
    if (it == json.end() || it->is_null()) {
        return std::nullopt;
    }
    return it->get<OverlayAction>();
}

auto ParseLabel(const nlohmann::json& json, bool centered) -> LabelElement {
    return {
        .rect = Rect(json),
        .corner_radius = CornerRadius(json),
        .font_size = json.at("font_size").get<int>(),
        .fg_color = json.at("fg_color").get<std::string>(),
        .content = json.get<LabelContent>(),
        .centered = centered,
        .region = RegionFrom(json),
        .form = FormFrom(json),
    };
}

auto ScreenActions(const std::optional<OverlayAction>& action, OverlayId id) -> std::optional<std::vector<ButtonAction>> {
    if (!action) {
        return std::nullopt;
    }
    return std::vector<ButtonAction>{OverlayButtonAction{.target = id, .action = *action}};
}

auto Advance(ListLayout layout, float& x, float& y, float w, float h) -> void {
    if (layout == ListLayout::Horizontal) {
        x += w;
    } else {
        y += h;
    }
}

auto Scaled(const std::array<float, 4>& rect, float scale) -> std::array<float, 4> {
    return {rect[0] * scale, rect[1] * scale, rect[2] * scale, rect[3] * scale};
}

auto ScaledFont(int font_size, float scale) -> int {
    return static_cast<int>(std::lround(static_cast<float>(font_size) * scale));
}

auto Radius(std::optional<float> custom, Shape shape, float scale) -> float {
    return Theme::Resolve(custom, g_themes->Active().rounding, shape) * scale;
}

auto PutPlacement(nlohmann::ordered_json& json, const std::array<float, 4>& rect, Region region, size_t form) -> void {
    if (region == Region::Toolbar) {
        json["region"] = NameOf(region, kRegionNames);
        json["width"] = Json::ShortestFloat(rect[2]);
        return;
    }
    if (form > 0) {
        json["form"] = form;
    }
    json["rect"] = {
        Json::ShortestFloat(rect[0]),
        Json::ShortestFloat(rect[1]),
        Json::ShortestFloat(rect[2]),
        Json::ShortestFloat(rect[3]),
    };
}

auto PutRadius(nlohmann::ordered_json& json, std::optional<float> radius) -> void {
    if (radius) {
        json["corner_radius"] = Json::ShortestFloat(*radius);
    }
}

auto PutOverlayAction(nlohmann::ordered_json& json, std::string_view key, const std::optional<OverlayAction>& action) -> void {
    if (action) {
        json[key] = *action;
    }
}

auto Merge(nlohmann::ordered_json& json, const nlohmann::ordered_json& extra) -> void {
    for (const auto& [key, value] : extra.items()) {
        json[key] = value;
    }
}

} // namespace

auto from_json(const nlohmann::json& json, ModularElement& value) -> void {
    const auto type = json.at("type").get<std::string>();
    if (type == "Panel") {
        value = PanelElement{
            .rect = Rect(json),
            .corner_radius = CornerRadius(json),
            .bg_color = json.at("bg_color").get<std::string>(),
            .region = RegionFrom(json),
            .form = FormFrom(json),
        };
    } else if (type == "Label") {
        value = ParseLabel(json, false);
    } else if (type == "CenteredLabel") {
        value = ParseLabel(json, true);
    } else if (type == "Sprite") {
        SpriteElement sprite{
            .rect = Rect(json),
            .sprite = json.at("sprite").get<std::string>(),
            .region = RegionFrom(json),
            .form = FormFrom(json),
        };
        if (const auto it = json.find("sprite_st"); it != json.end() && !it->is_null()) {
            sprite.sprite_st = it->get<std::array<float, 4>>();
        }
        value = std::move(sprite);
    } else if (type == "Button") {
        value = ButtonElement{
            .rect = Rect(json),
            .corner_radius = CornerRadius(json),
            .font_size = json.at("font_size").get<int>(),
            .fg_color = json.at("fg_color").get<std::string>(),
            .bg_color = json.at("bg_color").get<std::string>(),
            .text = json.at("text").get<std::string>(),
            .data = json.get<ButtonData>(),
            .region = RegionFrom(json),
            .form = FormFrom(json),
        };
    } else if (type == "BatteryList") {
        value = BatteryListElement{
            .rect = Rect(json),
            .corner_radius = CornerRadius(json),
            .font_size = json.at("font_size").get<int>(),
            .fg_color = json.at("fg_color").get<std::string>(),
            .fg_color_low = json.at("fg_color_low").get<std::string>(),
            .fg_color_charging = json.at("fg_color_charging").get<std::string>(),
            .low_threshold = json.at("low_threshold").get<float>(),
            .num_devices = json.at("num_devices").get<size_t>(),
            .layout = Layout(json),
            .region = RegionFrom(json),
            .form = FormFrom(json),
        };
    } else if (type == "OverlayList") {
        value = OverlayListElement{
            .rect = Rect(json),
            .corner_radius = CornerRadius(json),
            .font_size = json.at("font_size").get<int>(),
            .fg_color = json.at("fg_color").get<std::string>(),
            .bg_color = json.at("bg_color").get<std::string>(),
            .layout = Layout(json),
            .overlay_template =
                {
                    .click_down = OptionalOverlayAction(json, "click_down"),
                    .click_up = OptionalOverlayAction(json, "click_up"),
                    .long_click_up = OptionalOverlayAction(json, "long_click_up"),
                    .right_down = OptionalOverlayAction(json, "right_down"),
                    .right_up = OptionalOverlayAction(json, "right_up"),
                    .long_right_up = OptionalOverlayAction(json, "long_right_up"),
                    .middle_down = OptionalOverlayAction(json, "middle_down"),
                    .middle_up = OptionalOverlayAction(json, "middle_up"),
                    .long_middle_up = OptionalOverlayAction(json, "long_middle_up"),
                    .scroll_down = OptionalOverlayAction(json, "scroll_down"),
                    .scroll_up = OptionalOverlayAction(json, "scroll_up"),
                },
            .region = RegionFrom(json),
            .form = FormFrom(json),
        };
    } else {
        throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for ModularElement", type), &json);
    }
}

auto from_json(const nlohmann::json& json, ModularUiConfig& value) -> void {
    value.SetWidth(json.at("width").get<float>());
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
            layout.position = ParseName(*position, kToolbarPositions, "ToolbarPosition");
        }
        value.Form().toolbar = layout;
    }
    value.SetElements(json.at("elements").get<std::vector<ModularElement>>());
}

auto to_json(nlohmann::ordered_json& json, const ModularElement& value) -> void {
    json = nlohmann::ordered_json::object();
    json["type"] = ModularElements::TypeName(value);
    PutPlacement(json, ModularElements::Rect(value), ModularElements::RegionOf(value), ModularElements::FormOf(value));
    std::visit(
        [&json](const auto& item) -> void {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, PanelElement>) {
                PutRadius(json, item.corner_radius);
                json["bg_color"] = item.bg_color;
            } else if constexpr (std::is_same_v<Item, LabelElement>) {
                PutRadius(json, item.corner_radius);
                json["font_size"] = item.font_size;
                json["fg_color"] = item.fg_color;
                Merge(json, nlohmann::ordered_json(item.content));
            } else if constexpr (std::is_same_v<Item, SpriteElement>) {
                json["sprite"] = item.sprite;
                if (item.sprite_st) {
                    const auto& st = *item.sprite_st;
                    json["sprite_st"] = {
                        Json::ShortestFloat(st[0]),
                        Json::ShortestFloat(st[1]),
                        Json::ShortestFloat(st[2]),
                        Json::ShortestFloat(st[3]),
                    };
                }
            } else if constexpr (std::is_same_v<Item, ButtonElement>) {
                PutRadius(json, item.corner_radius);
                json["font_size"] = item.font_size;
                json["fg_color"] = item.fg_color;
                json["bg_color"] = item.bg_color;
                json["text"] = item.text;
                Merge(json, nlohmann::ordered_json(item.data));
            } else if constexpr (std::is_same_v<Item, BatteryListElement>) {
                PutRadius(json, item.corner_radius);
                json["font_size"] = item.font_size;
                json["fg_color"] = item.fg_color;
                json["fg_color_low"] = item.fg_color_low;
                json["fg_color_charging"] = item.fg_color_charging;
                json["num_devices"] = item.num_devices;
                json["layout"] = item.layout == ListLayout::Horizontal ? "Horizontal" : "Vertical";
                json["low_threshold"] = Json::ShortestFloat(item.low_threshold);
            } else {
                PutRadius(json, item.corner_radius);
                json["font_size"] = item.font_size;
                json["fg_color"] = item.fg_color;
                json["bg_color"] = item.bg_color;
                json["layout"] = item.layout == ListLayout::Horizontal ? "Horizontal" : "Vertical";
                const auto& actions = item.overlay_template;
                PutOverlayAction(json, "click_down", actions.click_down);
                PutOverlayAction(json, "click_up", actions.click_up);
                PutOverlayAction(json, "long_click_up", actions.long_click_up);
                PutOverlayAction(json, "right_down", actions.right_down);
                PutOverlayAction(json, "right_up", actions.right_up);
                PutOverlayAction(json, "long_right_up", actions.long_right_up);
                PutOverlayAction(json, "middle_down", actions.middle_down);
                PutOverlayAction(json, "middle_up", actions.middle_up);
                PutOverlayAction(json, "long_middle_up", actions.long_middle_up);
                PutOverlayAction(json, "scroll_down", actions.scroll_down);
                PutOverlayAction(json, "scroll_up", actions.scroll_up);
            }
        },
        value
    );
}

auto to_json(nlohmann::ordered_json& json, const ModularUiConfig& value) -> void {
    json = nlohmann::ordered_json::object();
    json["width"] = Json::ShortestFloat(value.Width());
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
            {"position", NameOf(toolbar.position, kToolbarPositions)},
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
    json["elements"] = value.Elements();
}

auto Modular::CreateCanvas(const ModularUiConfig& config, float scale) -> Result<std::unique_ptr<Canvas>> {
    const auto size = config.Size();
    const auto width = static_cast<uint32_t>(std::lround(static_cast<float>(size[0]) * scale));
    const auto height = static_cast<uint32_t>(std::lround(static_cast<float>(size[1]) * scale));
    ASSIGN_OR_RETURN(auto canvas, Canvas::Create(width, height, g_device->NativeFormat()));
    Fill(*canvas, config, scale);
    return canvas;
}

auto Modular::Fill(Canvas& canvas, const ModularUiConfig& config, float scale, const Eigen::Vector2f& origin) -> void {
    const auto backgrounds = config.Backgrounds(config.Origin());
    FillArranged(canvas, backgrounds, scale, origin);
    const auto arranged = config.Arrange();
    FillArranged(canvas, arranged, scale, origin);
}

auto Modular::FillArranged(Canvas& canvas, std::span<const ModularElement> arranged, float scale, const Eigen::Vector2f& origin)
    -> void {
    for (const auto& element : arranged) {
        Build(canvas, element, scale, origin);
    }
}

auto Modular::Build(Canvas& canvas, const ModularElement& element, float scale, const Eigen::Vector2f& origin) -> void {
    std::visit(
        [&](const auto& item) -> void {
            using Item = std::decay_t<decltype(item)>;
            const auto& rect = item.rect;
            const auto [x, y, w, h] = Scaled({rect[0] + origin.x(), rect[1] + origin.y(), rect[2], rect[3]}, scale);
            if constexpr (std::is_same_v<Item, PanelElement>) {
                canvas.SetBgColor(Color(item.bg_color));
                canvas.Panel(x, y, w, h, Radius(item.corner_radius, Shape::Panel, scale));
            } else if constexpr (std::is_same_v<Item, LabelElement>) {
                if (const auto* clock = std::get_if<ClockLabelContent>(&item.content); clock != nullptr && clock->city_size) {
                    const float city = static_cast<float>(*clock->city_size) * scale;
                    const float above = (static_cast<float>(item.font_size) + kCityGap) * scale;
                    canvas.SetFontSize(ScaledFont(*clock->city_size, scale));
                    canvas.SetFgColor(Color(item.fg_color));
                    if (item.centered) {
                        canvas.LabelCentered(x, y - city * kCityLine, w, city * kCityLine, 0.0f, Labels::City(clock->timezone));
                    } else {
                        canvas.Label(x, y - above, w, city * kCityLine, 0.0f, Labels::City(clock->timezone));
                    }
                }
                canvas.SetFontSize(ScaledFont(item.font_size, scale));
                canvas.SetFgColor(Color(item.fg_color));
                const float radius = Radius(item.corner_radius, Shape::Control, scale);
                auto setup = Labels::Create(item.content, canvas.FgColor());
                auto& label = item.centered
                                ? canvas.LabelCentered(x, y, w, h, radius, std::move(setup.text), std::move(setup.control))
                                : canvas.Label(x, y, w, h, radius, std::move(setup.text), std::move(setup.control));
                if (setup.color) {
                    label.SetFgColor(*setup.color);
                }
            } else if constexpr (std::is_same_v<Item, SpriteElement>) {
                if (auto image = SpriteFromPath(item.sprite)) {
                    auto& sprite = canvas.Sprite(x, y, w, h);
                    sprite.SetFgColor(Eigen::Vector4f::Ones());
                    sprite.SetSprite(std::move(*image));
                    const auto st = item.sprite_st.value_or(std::array{1.0f, 1.0f, 0.0f, 0.0f});
                    sprite.SetSpriteSt(Eigen::Vector4f(st[0], st[1], st[2], st[3]));
                } else {
                    LOG_WARN("Could not load custom UI sprite: {}", image.error().Message());
                }
            } else if constexpr (std::is_same_v<Item, ButtonElement>) {
                canvas.SetBgColor(Color(item.bg_color));
                canvas.SetFgColor(Color(item.fg_color));
                canvas.SetFontSize(ScaledFont(item.font_size, scale));
                canvas.Button(
                    x,
                    y,
                    w,
                    h,
                    Radius(item.corner_radius, Shape::Control, scale),
                    item.text,
                    std::make_unique<ModularButton>(item.data)
                );
            } else if constexpr (std::is_same_v<Item, BatteryListElement>) {
                const auto count = static_cast<float>(std::max<size_t>(item.num_devices, 1));
                const float button_w = item.layout == ListLayout::Horizontal ? w / count : w;
                const float button_h = item.layout == ListLayout::Horizontal ? h : h / count;
                const float low_threshold = item.low_threshold * 0.01f;
                const float inset = 2.0f * scale;
                const float radius = Radius(item.corner_radius, Shape::Control, scale);
                const auto fg_color = Color(item.fg_color);
                canvas.SetFontSize(ScaledFont(item.font_size, scale));
                canvas.SetFgColor(fg_color);

                float button_x = x;
                float button_y = y;
                for (size_t i = 0; i < item.num_devices; ++i) {
                    const LabelContent content = BatteryLabelContent{
                        .device = i,
                        .low_threshold = low_threshold,
                        .low_color = item.fg_color_low,
                        .charging_color = item.fg_color_charging,
                    };
                    auto setup = Labels::Create(content, fg_color);
                    canvas.LabelCentered(
                        button_x + inset,
                        button_y + inset,
                        button_w - 2.0f * inset,
                        button_h - 2.0f * inset,
                        radius,
                        std::move(setup.text),
                        std::move(setup.control)
                    );
                    Advance(item.layout, button_x, button_y, button_w, button_h);
                }
            } else {
                const auto count = static_cast<float>(std::max<size_t>(g_overlays->Screens().size(), 1));
                const float button_w = item.layout == ListLayout::Horizontal ? w / count : w;
                const float button_h = item.layout == ListLayout::Horizontal ? h : h / count;
                const float inset = 2.0f * scale;
                const float radius = Radius(item.corner_radius, Shape::Control, scale);
                canvas.SetBgColor(Color(item.bg_color));
                canvas.SetFgColor(Color(item.fg_color));
                canvas.SetFontSize(ScaledFont(item.font_size, scale));

                float button_x = x;
                float button_y = y;
                const auto& actions = item.overlay_template;
                for (const auto& screen : g_overlays->Screens()) {
                    ButtonData data{
                        .click_down = ScreenActions(actions.click_down, screen.id),
                        .click_up = ScreenActions(actions.click_up, screen.id),
                        .long_click_up = ScreenActions(actions.long_click_up, screen.id),
                        .right_down = ScreenActions(actions.right_down, screen.id),
                        .right_up = ScreenActions(actions.right_up, screen.id),
                        .long_right_up = ScreenActions(actions.long_right_up, screen.id),
                        .middle_down = ScreenActions(actions.middle_down, screen.id),
                        .middle_up = ScreenActions(actions.middle_up, screen.id),
                        .long_middle_up = ScreenActions(actions.long_middle_up, screen.id),
                        .scroll_down = ScreenActions(actions.scroll_down, screen.id),
                        .scroll_up = ScreenActions(actions.scroll_up, screen.id),
                    };
                    canvas.Button(
                        button_x + inset,
                        button_y + inset,
                        button_w - 2.0f * inset,
                        button_h - 2.0f * inset,
                        radius,
                        screen.name,
                        std::make_unique<ModularButton>(std::move(data))
                    );
                    Advance(item.layout, button_x, button_y, button_w, button_h);
                }
            }
        },
        element
    );
}

auto Modular::SpriteFromPath(const std::string& path) -> Result<std::shared_ptr<Image>> {
    const auto contents = ConfigStore::ReadFile(ConfigStore::Root() / path);
    if (!contents) {
        return Unexpected(std::format("Could not open custom sprite at: {}", path));
    }
    ASSIGN_OR_RETURN(auto command_buffer, g_device->CreateCommandBuffer(g_device->TransferQueue()));
    auto image = command_buffer.UploadDds(std::as_bytes(std::span(*contents)));
    if (!image) {
        return Unexpected(std::format("Could not use custom sprite at: {}\n{}", path, image.error().Message()));
    }
    RETURN_IF_ERROR(command_buffer.SubmitUpload());
    return std::move(*image);
}
