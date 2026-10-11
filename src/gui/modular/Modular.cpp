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
#include <format>
#include <utility>

#include "CommandBuffer.hpp"
#include "Config.hpp"
#include "Device.hpp"
#include "Json.hpp"
#include "Logger.hpp"
#include "State.hpp"
#include "Theme.hpp"
#include "framexr_config.h"

namespace {

auto Color(const std::string& hex) -> Eigen::Vector4f {
    return Canvas::ParseColor(hex).value_or(Canvas::FallbackColor());
}

auto Radius(std::optional<float> custom, Shape shape) -> float {
    return Theme::Resolve(custom, Theme::Active().rounding, shape);
}

auto Rect(const nlohmann::json& json) -> std::array<float, 4> {
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

} // namespace

auto from_json(const nlohmann::json& json, ModularElement& value) -> void {
    const auto type = json.at("type").get<std::string>();
    if (type == "Panel") {
        value = PanelElement{
            .rect = Rect(json),
            .corner_radius = CornerRadius(json),
            .bg_color = json.at("bg_color").get<std::string>()
        };
    } else if (type == "Label") {
        value = ParseLabel(json, false);
    } else if (type == "CenteredLabel") {
        value = ParseLabel(json, true);
    } else if (type == "Sprite") {
        SpriteElement sprite{.rect = Rect(json), .sprite = json.at("sprite").get<std::string>()};
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
        };
    } else if (type == "OverlayList") {
        value = OverlayListElement{
            .rect = Rect(json),
            .corner_radius = CornerRadius(json),
            .font_size = json.at("font_size").get<int>(),
            .fg_color = json.at("fg_color").get<std::string>(),
            .bg_color = json.at("bg_color").get<std::string>(),
            .layout = Layout(json),
            .overlay_template = {
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
        };
    } else {
        throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for ModularElement", type), &json);
    }
}

auto from_json(const nlohmann::json& json, ModularUiConfig& value) -> void {
    value.width = json.at("width").get<float>();
    value.size = json.at("size").get<std::array<uint32_t, 2>>();
    if (const auto it = json.find("spawn_pos"); it != json.end() && !it->is_null()) {
        value.spawn_pos = it->get<std::array<float, 3>>();
    }
    value.elements = json.at("elements").get<std::vector<ModularElement>>();
}

auto Modular::CreateCanvas(const std::array<uint32_t, 2>& size, const std::vector<ModularElement>& elements, AppState& app)
    -> Result<std::unique_ptr<Canvas>> {
    ASSIGN_OR_RETURN(auto canvas, Canvas::Create(app.device, size[0], size[1], app.device->NativeFormat()));
    for (const auto& element : elements) {
        Build(*canvas, element, app);
    }
    return canvas;
}

auto Modular::Build(Canvas& canvas, const ModularElement& element, AppState& app) -> void {
    std::visit(
        [&](const auto& item) -> void {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, PanelElement>) {
                const auto& [x, y, w, h] = item.rect;
                canvas.bg_color = Color(item.bg_color);
                canvas.Panel(x, y, w, h, Radius(item.corner_radius, Shape::Panel));
            } else if constexpr (std::is_same_v<Item, LabelElement>) {
                const auto& [x, y, w, h] = item.rect;
                canvas.font_size = item.font_size;
                canvas.fg_color = Color(item.fg_color);
                const float radius = Radius(item.corner_radius, Shape::Control);
                auto setup = Labels::Create(item.content, app, canvas.fg_color);
                auto& label = item.centered
                                ? canvas.LabelCentered(x, y, w, h, radius, std::move(setup.text), std::move(setup.control))
                                : canvas.Label(x, y, w, h, radius, std::move(setup.text), std::move(setup.control));
                if (setup.color) {
                    label.SetFgColor(*setup.color);
                }
            } else if constexpr (std::is_same_v<Item, SpriteElement>) {
                const auto& [x, y, w, h] = item.rect;
                if (auto image = SpriteFromPath(item.sprite, app)) {
                    auto& sprite = canvas.Sprite(x, y, w, h);
                    sprite.fg_color = Eigen::Vector4f::Ones();
                    sprite.SetSprite(std::move(*image));
                    const auto st = item.sprite_st.value_or(std::array{1.0f, 1.0f, 0.0f, 0.0f});
                    sprite.SetSpriteSt(Eigen::Vector4f(st[0], st[1], st[2], st[3]));
                } else {
                    LOG_WARN("Could not load custom UI sprite: {}", image.error().Message());
                }
            } else if constexpr (std::is_same_v<Item, ButtonElement>) {
                const auto& [x, y, w, h] = item.rect;
                canvas.bg_color = Color(item.bg_color);
                canvas.fg_color = Color(item.fg_color);
                canvas.font_size = item.font_size;
                canvas.Button(
                    x,
                    y,
                    w,
                    h,
                    Radius(item.corner_radius, Shape::Control),
                    item.text,
                    std::make_unique<ModularButton>(item.data)
                );
            } else if constexpr (std::is_same_v<Item, BatteryListElement>) {
                const auto& [x, y, w, h] = item.rect;
                const auto count = static_cast<float>(item.num_devices);
                const float button_w = item.layout == ListLayout::Horizontal ? w / count : w;
                const float button_h = item.layout == ListLayout::Horizontal ? h : h / count;
                const float low_threshold = item.low_threshold * 0.01f;
                const float radius = Radius(item.corner_radius, Shape::Control);
                const auto fg_color = Color(item.fg_color);
                canvas.font_size = item.font_size;
                canvas.fg_color = fg_color;

                float button_x = x;
                float button_y = y;
                for (size_t i = 0; i < item.num_devices; ++i) {
                    const LabelContent content = BatteryLabelContent{
                        .device = i,
                        .low_threshold = low_threshold,
                        .low_color = item.fg_color_low,
                        .charging_color = item.fg_color_charging,
                    };
                    auto setup = Labels::Create(content, app, fg_color);
                    canvas.LabelCentered(
                        button_x + 2.0f,
                        button_y + 2.0f,
                        button_w - 4.0f,
                        button_h - 4.0f,
                        radius,
                        std::move(setup.text),
                        std::move(setup.control)
                    );
                    Advance(item.layout, button_x, button_y, button_w, button_h);
                }
            } else {
                const auto& [x, y, w, h] = item.rect;
                const auto count = static_cast<float>(app.screens.size());
                const float button_w = item.layout == ListLayout::Horizontal ? w / count : w;
                const float button_h = item.layout == ListLayout::Horizontal ? h : h / count;
                const float radius = Radius(item.corner_radius, Shape::Control);
                canvas.bg_color = Color(item.bg_color);
                canvas.fg_color = Color(item.fg_color);
                canvas.font_size = item.font_size;

                float button_x = x;
                float button_y = y;
                const auto& actions = item.overlay_template;
                for (const auto& screen : app.screens) {
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
                        button_x + 2.0f,
                        button_y + 2.0f,
                        button_w - 4.0f,
                        button_h - 4.0f,
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

auto Modular::SpriteFromPath(const std::string& path, AppState& app) -> Result<std::shared_ptr<Image>> {
    if (const auto it = app.sprites.find(path); it != app.sprites.end()) {
        return it->second;
    }

    const auto contents = Config::ReadFile(Config::Root() / path);
    if (!contents) {
        return Unexpected(std::format("Could not open custom sprite at: {}", path));
    }

    auto& device = *app.device;
    ASSIGN_OR_RETURN(auto command_buffer, device.CreateCommandBuffer(device.TransferQueue()));
    auto image = command_buffer.UploadDds(std::as_bytes(std::span(*contents)));
    if (!image) {
        return Unexpected(std::format("Could not use custom sprite at: {}\n{}", path, image.error().Message()));
    }
    RETURN_IF_ERROR(command_buffer.SubmitUpload());
    return std::move(*image);
}
