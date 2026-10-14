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

#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "document/Actions.hpp"
#include "Config.hpp"
#include "ConfigStore.hpp"
#include "document/FormElements.hpp"
#include "FormTheme.hpp"
#include "Yaml.hpp"
#include "document/FormDocument.hpp"

namespace
{

constexpr char kWatchYaml[] = {
#embed "watch.yaml"
    ,
    '\0'
};
constexpr char kSettingsYaml[] = {
#embed "settings.yaml"
    ,
    '\0'
};

constexpr std::string_view kEveryActionYaml = R"(
width: 0.2
spawn_pos: [0, -0.1, -0.5]
form:
  width: 300
  height: 100
  toolbar:
    height: 30
    gap: 4
    padding: 3
    position: Above
components:
  - type: Panel
    rect: [0, 0, 300, 100]
    corner_radius: 12
    bg_color: "#24273a"
  - type: Clock
    rect: [0, 0, 100, 20]
    font_size: 12
    fg_color: "#cad3f5"
    centered: true
    format: "%H:%M"
    timezone: "Europe/Helsinki"
  - type: Battery
    rect: [0, 30, 100, 20]
    font_size: 12
    fg_color: "#cad3f5"
    device: 1
    low_threshold: 20
    low_color: "#ff0000"
    charging_color: "#00ff00"
  - type: Exec
    rect: [0, 60, 100, 20]
    font_size: 12
    fg_color: "#cad3f5"
    command: ["date"]
    interval: 2.5
  - type: Button
    region: Toolbar
    width: 60
    font_size: 14
    fg_color: "#000000"
    bg_color: "#ffffff"
    text: "All"
    click_down:
      - type: Exec
        command: ["true"]
        toast: "ran"
      - type: VirtualKey
        keycode: Escape
        action: Release
      - type: Watch
        action:
          Rotation: { axis: Y, delta: 5 }
      - type: Overlay
        target: 3
        action:
          Opacity: { delta: 0.5 }
      - type: Window
        target: settings
        action: ShowUi
      - type: Toast
        message: hi
        body: there
        seconds: 2
      - type: ColorAdjust
        channel: G
        delta: -0.1
      - type: DragMultiplier
        delta: 0.25
      - type: System
        action: ShowHide
      - type: SendOscValue
        parameter: /avatar/x
        values:
          - { type: Int, value: 3 }
          - { type: Float, value: 0.5 }
          - { type: String, value: "a" }
          - { type: Bool, value: true }
      - type: Shortcut
        keys: [LCtrl, C]
      - type: TypeText
        text: "true"
      - type: KeyboardProfile
        profile: full-size
    right_up:
      - type: Watch
        action: SwitchHands
    highlight: Notifications
  - type: OverlayList
    region: Toolbar
    width: 120
    font_size: 14
    fg_color: "#ffffff"
    bg_color: "#000000"
    layout: Horizontal
    click_up: ToggleVisible
    scroll_up:
      Opacity: { delta: 0.025 }
  - type: BatteryList
    rect: [100, 0, 200, 30]
    font_size: 12
    fg_color: "#ffffff"
    fg_color_low: "#ff0000"
    fg_color_charging: "#00ff00"
    num_devices: 4
    layout: Vertical
    low_threshold: 33
)";

auto Parse(std::string_view text) -> FormDocument
{
    const auto parsed = Yaml::Parse(text);
    EXPECT_TRUE(parsed.has_value());
    return parsed ? parsed->get<FormDocument>() : FormDocument{};
}

auto Emit(const FormDocument& config) -> std::string
{
    return Yaml::Emit(nlohmann::ordered_json(config));
}

auto ToolbarRects(const FormDocument& config) -> std::vector<std::array<float, 4>>
{
    std::vector<std::array<float, 4>> rects;
    const auto arranged = config.Arrange();
    for (size_t i = 0; i < config.Components().size(); ++i)
    {
        if (FormElements::RegionOf(config.Components()[i]) == Region::Toolbar)
        {
            rects.push_back(FormElements::Rect(arranged[i]));
        }
    }
    return rects;
}

} // namespace

TEST(FormDocument, WatchHasAFormAndAToolbarBelowIt)
{
    const auto watch = ConfigStore::LoadKnownYaml<FormDocument>("watch.yaml", kWatchYaml);
    ASSERT_TRUE(watch.Form().toolbar.has_value());
    EXPECT_EQ(watch.Size(), (std::array<uint32_t, 2>{400, 200}));

    const auto rects = ToolbarRects(watch);
    ASSERT_GE(rects.size(), 5U);
    float previous_right = 0.0f;
    for (const auto& [x, y, w, h] : rects)
    {
        EXPECT_GE(x, previous_right);
        EXPECT_GE(y, 160.0f);
        EXPECT_LE(y + h, 200.0f);
        previous_right = x + w;
    }
    EXPECT_LE(previous_right, 400.0f);
}

TEST(FormDocument, FormElementsAreRelativeToTheForm)
{
    auto config = Parse(kEveryActionYaml);
    EXPECT_EQ(config.Size(), (std::array<uint32_t, 2>{300, 134}));
    EXPECT_FLOAT_EQ(config.FormRect().y, 34.0f);
    EXPECT_FLOAT_EQ(FormElements::Rect(config.Arrange()[0])[1], 34.0f);

    config.Form().toolbar->position = ToolbarPosition::Below;
    EXPECT_FLOAT_EQ(config.FormRect().y, 0.0f);
    EXPECT_FLOAT_EQ(config.ToolbarRect()->y, 104.0f);
    EXPECT_FLOAT_EQ(FormElements::Rect(config.Arrange()[0])[1], 0.0f);
}

TEST(FormDocument, ToolbarSpreadsItemsEvenlyAndPadsButtons)
{
    const auto config = Parse(kEveryActionYaml);
    const auto rects = ToolbarRects(config);
    ASSERT_EQ(rects.size(), 2U);
    const float gap = (300.0f - 180.0f) / 3.0f;
    EXPECT_FLOAT_EQ(rects[0][0], gap);
    EXPECT_FLOAT_EQ(rects[0][1], 3.0f);
    EXPECT_FLOAT_EQ(rects[0][3], 24.0f);
    EXPECT_FLOAT_EQ(rects[1][0], gap * 2.0f + 60.0f);
    EXPECT_FLOAT_EQ(rects[1][1], 0.0f);
    EXPECT_FLOAT_EQ(rects[1][3], 30.0f);
}

TEST(FormDocument, CrowdedToolbarShrinksItemsToFit)
{
    auto config = Parse(kEveryActionYaml);
    config.Form().width = 100.0f;
    const auto rects = ToolbarRects(config);
    ASSERT_EQ(rects.size(), 2U);
    EXPECT_FLOAT_EQ(rects[0][0], 2.0f);
    EXPECT_NEAR(rects[1][0] + rects[1][2], 98.0f, 1e-4f);
    EXPECT_NEAR(rects[1][2] / rects[0][2], 2.0f, 1e-4f);
}

TEST(FormDocument, FormWithoutAToolbarFillsTheCanvas)
{
    const auto settings = ConfigStore::LoadKnownYaml<FormDocument>("settings.yaml", kSettingsYaml);
    EXPECT_FALSE(settings.Form().toolbar.has_value());
    EXPECT_EQ(settings.Size(), (std::array<uint32_t, 2>{600, 700}));
    const auto arranged = settings.Arrange();
    ASSERT_EQ(arranged.size(), settings.Components().size());
    for (size_t i = 0; i < arranged.size(); ++i)
    {
        EXPECT_EQ(FormElements::Rect(arranged[i]), FormElements::Rect(settings.Components()[i]));
    }
}

TEST(FormDocument, EveryElementAndActionSurvivesASaveAndLoad)
{
    const auto config = Parse(kEveryActionYaml);
    const auto saved = Emit(config);
    const auto reloaded = Parse(saved);
    EXPECT_EQ(Emit(reloaded), saved);
    EXPECT_EQ(reloaded.Components().size(), config.Components().size());
    EXPECT_NE(saved.find("SendOscValue"), std::string::npos);
    EXPECT_NE(saved.find("rect: [0, 0, 300, 100]"), std::string::npos);
    EXPECT_NE(saved.find("width: 120"), std::string::npos);

    const auto& button = std::get<ButtonElement>(reloaded.Components()[4]);
    ASSERT_TRUE(button.data.click_down.has_value());
    EXPECT_EQ(button.data.click_down->size(), 13U);
    EXPECT_EQ(button.data.highlight, HighlightTest::Notifications);
}

TEST(FormDocument, BundledLayoutsSurviveASaveAndLoad)
{
    for (const auto& [name, text] : {std::pair{"watch.yaml", std::string_view(kWatchYaml)}, {"settings.yaml", kSettingsYaml}})
    {
        const auto config = ConfigStore::LoadKnownYaml<FormDocument>(name, text);
        const auto saved = Emit(config);
        EXPECT_EQ(Emit(Parse(saved)), saved) << name;
    }
}

TEST(Theme, RoundingScalesTheDefaultRadiusAndCustomRadiusWins)
{
    EXPECT_FLOAT_EQ(FormTheme::Radius(Rounding::Square, Shape::Key), 0.0f);
    EXPECT_FLOAT_EQ(FormTheme::Radius(Rounding::Subtle, Shape::Control), FormTheme::Radius(Rounding::Round, Shape::Control) / 2.0f);
    EXPECT_GT(FormTheme::Radius(Rounding::Round, Shape::Panel), FormTheme::Radius(Rounding::Round, Shape::Control));
    EXPECT_FLOAT_EQ(FormTheme::Resolve(5.0f, Rounding::Square, Shape::Panel), 5.0f);
    EXPECT_FLOAT_EQ(FormTheme::Resolve(std::nullopt, Rounding::Round, Shape::Control), 8.0f);
    EXPECT_EQ(nlohmann::json(Rounding::Subtle).get<Rounding>(), Rounding::Subtle);
}
