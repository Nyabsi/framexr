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

#include <chrono>
#include <cmath>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "FormDesigner.hpp"
#include "document/FormElements.hpp"
#include "Keyboard.hpp"
#include "KeyboardDesigner.hpp"
#include "Yaml.hpp"

namespace
{

constexpr std::string_view kWatchYaml = R"(
width: 0.1
form:
  width: 200
  height: 100
  toolbar:
    height: 30
components:
  - { type: Panel, rect: [0, 0, 200, 100], bg_color: "#000000" }
  - { type: Button, rect: [20, 20, 40, 20], font_size: 12, fg_color: "#000000", bg_color: "#ffffff", text: "1" }
  - { type: Button, region: Toolbar, width: 40, font_size: 12, fg_color: "#000000", bg_color: "#ffffff", text: "A" }
  - { type: Button, region: Toolbar, width: 40, font_size: 12, fg_color: "#000000", bg_color: "#ffffff", text: "B" }
)";

constexpr std::string_view kKeyboardYaml = R"(
name: small
columns: 3
rows: 2
keys:
  - { at: [0, 0], key: A }
  - { at: [1, 0], key: B }
)";

auto Overlaps(const CanvasRect& a, const CanvasRect& b) -> bool
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

auto SmallWatch() -> FormDesigner
{
    FormDesigner designer(Yaml::Parse(kWatchYaml)->get<FormDocument>());
    designer.SetHold({});
    return designer;
}

auto Drag(FormDesigner& designer, const Eigen::Vector2f& from, const Eigen::Vector2f& to) -> bool
{
    designer.Press(from);
    designer.Move((from + to) / 2.0f);
    designer.Move(to);
    return designer.Release(to);
}

auto Text(const FormElement& element) -> std::string
{
    const auto* button = std::get_if<ButtonElement>(&element);
    return button != nullptr ? button->text : std::string();
}

auto ToolbarTexts(const FormDocument& config) -> std::vector<std::string>
{
    std::vector<std::string> texts;
    for (const auto& element : config.Components())
    {
        if (FormElements::RegionOf(element) == Region::Toolbar)
        {
            texts.push_back(Text(element));
        }
    }
    return texts;
}

auto SmallKeyboard() -> KeyboardDesigner
{
    KeyboardDesigner designer(*KeyboardLayout::Parse(kKeyboardYaml));
    designer.SetHold({});
    return designer;
}

auto CellCenter(float x, int row, float width = 1.0f) -> Eigen::Vector2f
{
    const auto rect = Keyboard::KeyRect({.x = x, .row = row, .width = width}, {.scale = KeyboardDesigner::kScale});
    return {rect.x + rect.w / 2.0f, rect.y + rect.h / 2.0f};
}

auto Click(KeyboardDesigner& designer, const Eigen::Vector2f& point) -> bool
{
    designer.Press(point);
    return designer.Release(point);
}

} // namespace

TEST(FormDesigner, DraggingSnapsToTheFormCentreAndCanBeUndone)
{
    auto designer = SmallWatch();
    EXPECT_TRUE(Drag(designer, {40.0f, 30.0f}, {98.0f, 30.0f}));
    EXPECT_EQ(designer.Selected(), 1U);
    EXPECT_FLOAT_EQ(FormElements::Rect(designer.Document().Components()[1])[0], 80.0f);
    EXPECT_TRUE(designer.Revisions().Dirty());

    designer.Press({100.0f, 30.0f});
    designer.Move({104.0f, 30.0f});
    ASSERT_FALSE(designer.Guides().empty());
    EXPECT_TRUE(designer.Guides().front().vertical);
    designer.SetSnap(false);
    designer.Move({100.0f, 30.0f});
    EXPECT_TRUE(designer.Guides().empty());
    designer.Release({100.0f, 30.0f});

    EXPECT_TRUE(designer.Revisions().Undo());
    EXPECT_TRUE(designer.Revisions().Undo());
    EXPECT_FLOAT_EQ(FormElements::Rect(designer.Document().Components()[1])[0], 20.0f);
    EXPECT_FALSE(designer.Revisions().Dirty());
}

TEST(FormDesigner, QuickClicksNeverMoveComponents)
{
    FormDesigner designer(Yaml::Parse(kWatchYaml)->get<FormDocument>());
    designer.Press({40.0f, 30.0f});
    designer.Move({80.0f, 60.0f});
    EXPECT_FALSE(designer.Engaged());
    EXPECT_FALSE(designer.Release({80.0f, 60.0f}));
    EXPECT_EQ(designer.Selected(), 1U);
    EXPECT_FLOAT_EQ(FormElements::Rect(designer.Document().Components()[1])[0], 20.0f);
    EXPECT_FALSE(designer.Revisions().CanUndo());
}

TEST(FormDesigner, ClickingTheSelectionAgainClosesIt)
{
    auto designer = SmallWatch();
    designer.Press({40.0f, 30.0f});
    EXPECT_FALSE(designer.Release({40.0f, 30.0f}));
    EXPECT_EQ(designer.Selected(), 1U);
    designer.Press({40.0f, 30.0f});
    EXPECT_FALSE(designer.Release({40.0f, 30.0f}));
    EXPECT_FALSE(designer.Selected().has_value());

    EXPECT_TRUE(Drag(designer, {40.0f, 30.0f}, {98.0f, 30.0f}));
    EXPECT_TRUE(Drag(designer, {100.0f, 30.0f}, {60.0f, 30.0f}));
    EXPECT_EQ(designer.Selected(), 1U);

    designer.Insert(FormPart::Form, std::nullopt);
    const auto form = designer.FormBounds(1);
    const Eigen::Vector2f inside{form.x + 100.0f, form.y + 60.0f};
    EXPECT_EQ(designer.SelectedForm(), 1U);
    designer.Press(inside);
    designer.Release(inside);
    EXPECT_FALSE(designer.SelectedForm().has_value());
}

TEST(FormDesigner, HoldingLiftsAComponentBeforeItFollows)
{
    FormDesigner designer(Yaml::Parse(kWatchYaml)->get<FormDocument>());
    designer.SetSnap(false);
    designer.SetHold(std::chrono::milliseconds(30));
    designer.Press({40.0f, 30.0f});
    designer.Move({44.0f, 32.0f});
    EXPECT_FALSE(designer.Engaged());
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    designer.Move({46.0f, 33.0f});
    EXPECT_TRUE(designer.Engaged());
    EXPECT_FLOAT_EQ(FormElements::Rect(designer.Document().Components()[1])[0], 20.0f);
    designer.Move({56.0f, 33.0f});
    EXPECT_TRUE(designer.Release({56.0f, 33.0f}));
    EXPECT_FLOAT_EQ(FormElements::Rect(designer.Document().Components()[1])[0], 30.0f);
    EXPECT_FLOAT_EQ(FormElements::Rect(designer.Document().Components()[1])[1], 20.0f);
}

TEST(FormDesigner, ComponentsTrackTheLaserAndLandInsideTheForm)
{
    auto designer = SmallWatch();
    designer.Press({40.0f, 30.0f});
    designer.Move({0.0f, 30.0f});
    designer.Move({-50.0f, 25.0f});
    const auto& dragged = FormElements::Rect(designer.Document().Components()[1]);
    EXPECT_FLOAT_EQ(dragged[0], -70.0f);
    EXPECT_FLOAT_EQ(dragged[1], 15.0f);
    EXPECT_TRUE(designer.Release({-50.0f, 25.0f}));
    const auto& landed = FormElements::Rect(designer.Document().Components()[1]);
    EXPECT_FLOAT_EQ(landed[0], 0.0f);
    EXPECT_FLOAT_EQ(landed[1], 15.0f);
}

TEST(FormDesigner, SnappingHoldsOneAlignmentInsteadOfFlickering)
{
    auto config = Yaml::Parse(kWatchYaml)->get<FormDocument>();
    config.Components().emplace_back(PanelElement{.rect = {100.0f, 60.0f, 32.0f, 20.0f}, .bg_color = "#ffffff"});
    FormDesigner designer(std::move(config));
    designer.SetHold({});
    designer.Press({40.0f, 30.0f});
    designer.Move({60.0f, 30.0f});
    designer.Move({116.0f, 30.0f});
    const float first = FormElements::Rect(designer.Document().Components()[1])[0];
    EXPECT_FALSE(designer.Guides().empty());
    for (const float x : {115.0f, 117.0f, 115.5f, 116.5f, 115.0f, 117.0f})
    {
        designer.Move({x, 30.0f});
        EXPECT_FLOAT_EQ(FormElements::Rect(designer.Document().Components()[1])[0], first) << x;
    }
    designer.Move({170.0f, 30.0f});
    EXPECT_FLOAT_EQ(FormElements::Rect(designer.Document().Components()[1])[0], 150.0f);
    designer.Release({170.0f, 30.0f});
}

TEST(FormDesigner, ElementsStayInsideTheForm)
{
    auto designer = SmallWatch();
    Drag(designer, {40.0f, 30.0f}, {-200.0f, -200.0f});
    const auto& rect = FormElements::Rect(designer.Document().Components()[1]);
    EXPECT_FLOAT_EQ(rect[0], 0.0f);
    EXPECT_FLOAT_EQ(rect[1], 0.0f);
}

TEST(FormDesigner, ToolbarItemsReorderByDragging)
{
    auto designer = SmallWatch();
    EXPECT_EQ(ToolbarTexts(designer.Document()), (std::vector<std::string>{"A", "B"}));
    EXPECT_TRUE(Drag(designer, {60.0f, 115.0f}, {185.0f, 115.0f}));
    EXPECT_EQ(ToolbarTexts(designer.Document()), (std::vector<std::string>{"B", "A"}));
}

TEST(FormDesigner, ElementsMoveBetweenTheFormAndTheToolbar)
{
    auto designer = SmallWatch();
    EXPECT_TRUE(Drag(designer, {40.0f, 30.0f}, {190.0f, 115.0f}));
    EXPECT_EQ(ToolbarTexts(designer.Document()), (std::vector<std::string>{"A", "B", "1"}));

    EXPECT_TRUE(Drag(designer, {60.0f, 115.0f}, {100.0f, 50.0f}));
    EXPECT_EQ(ToolbarTexts(designer.Document()), (std::vector<std::string>{"B", "1"}));
    const auto moved = std::ranges::find_if(
        designer.Document().Components(),
        [](const FormElement& element) -> bool { return Text(element) == "A"; }
    );
    ASSERT_NE(moved, designer.Document().Components().end());
    EXPECT_EQ(FormElements::RegionOf(*moved), Region::Form);
    const auto& rect = FormElements::Rect(*moved);
    EXPECT_NEAR(rect[0] + rect[2] / 2.0f, 100.0f, 0.01f);
}

TEST(FormDesigner, CornersResizeTheFormToolbarAndElements)
{
    auto designer = SmallWatch();
    EXPECT_TRUE(Drag(designer, {200.0f, 100.0f}, {248.0f, 118.0f}));
    EXPECT_FLOAT_EQ(designer.Document().Form().width, 250.0f);
    EXPECT_FLOAT_EQ(designer.Document().Form().height, 120.0f);
    EXPECT_TRUE(Drag(designer, {250.0f, 150.0f}, {250.0f, 160.0f}));
    EXPECT_FLOAT_EQ(designer.Document().Form().toolbar->height, 40.0f);

    designer.Press({40.0f, 30.0f});
    designer.Release({40.0f, 30.0f});
    EXPECT_TRUE(Drag(designer, {60.0f, 40.0f}, {80.0f, 50.0f}));
    const auto& rect = FormElements::Rect(designer.Document().Components()[1]);
    EXPECT_FLOAT_EQ(rect[2], 60.0f);
    EXPECT_FLOAT_EQ(rect[3], 30.0f);
}

TEST(FormDesigner, DroppingOutsideEveryFormCancels)
{
    auto designer = SmallWatch();
    const auto count = designer.Document().Components().size();
    EXPECT_FALSE(designer.Insert(FormPart::Button, Eigen::Vector2f(-200.0f, -200.0f)).has_value());
    EXPECT_EQ(designer.Document().Components().size(), count);
    EXPECT_FALSE(designer.Revisions().CanUndo());
    EXPECT_TRUE(designer.Insert(FormPart::Button, Eigen::Vector2f(100.0f, 50.0f)).has_value());
    EXPECT_EQ(designer.Document().Components().size(), count + 1);
}

TEST(FormDesigner, ResizingAClockScalesItsCityToo)
{
    auto config = Yaml::Parse(kWatchYaml)->get<FormDocument>();
    config.Components().emplace_back(
        ClockElement{.rect = {20.0f, 60.0f, 0.0f, 0.0f}, .font_size = 12, .fg_color = "#ffffff", .format = "%H:%M", .city_size = 10}
    );
    FormDesigner designer(std::move(config));
    designer.SetHold({});
    designer.SetSnap(false);
    const size_t index = designer.Document().Components().size() - 1;
    designer.SetSelected(index);
    const auto rect = designer.VisualRect(index);
    const Eigen::Vector2f corner(rect.x + rect.w, rect.y + rect.h);
    ASSERT_EQ(designer.HitTest(corner).kind, FormHitKind::Resize);
    EXPECT_TRUE(Drag(designer, corner, corner + Eigen::Vector2f(0.0f, 12.0f)));
    const auto& clock = std::get<ClockElement>(designer.Document().Components()[index]);
    EXPECT_GT(clock.font_size, 12);
    ASSERT_TRUE(clock.city_size.has_value());
    EXPECT_EQ(*clock.city_size, static_cast<int>(std::lround(10.0f * static_cast<float>(clock.font_size) / 12.0f)));
}

TEST(FormDesigner, ResizingStaysBetweenTheContentAndTheForm)
{
    auto designer = SmallWatch();
    designer.SetSnap(false);
    designer.Press({40.0f, 30.0f});
    designer.Release({40.0f, 30.0f});
    EXPECT_TRUE(Drag(designer, {60.0f, 40.0f}, {21.0f, 21.0f}));
    const auto minimum = designer.MinimumSize(designer.Document().Components()[1]);
    auto rect = FormElements::Rect(designer.Document().Components()[1]);
    EXPECT_FLOAT_EQ(rect[2], minimum.x());
    EXPECT_FLOAT_EQ(rect[3], minimum.y());
    EXPECT_GE(rect[3], 20.0f);

    EXPECT_TRUE(Drag(designer, {20.0f + rect[2], 20.0f + rect[3]}, {900.0f, 900.0f}));
    rect = FormElements::Rect(designer.Document().Components()[1]);
    EXPECT_FLOAT_EQ(rect[0] + rect[2], 200.0f);
    EXPECT_FLOAT_EQ(rect[1] + rect[3], 100.0f);

    EXPECT_TRUE(Drag(designer, {200.0f, 100.0f}, {10.0f, 10.0f}));
    EXPECT_FLOAT_EQ(designer.Document().Form().width, 200.0f);
    EXPECT_FLOAT_EQ(designer.Document().Form().height, 100.0f);
}

TEST(FormDesigner, LabelsAreMeasuredByTheirText)
{
    auto config = Yaml::Parse(kWatchYaml)->get<FormDocument>();
    config.Components().emplace_back(
        LabelElement{
            .rect = {150.0f, 40.0f, 200.0f, 50.0f},
            .font_size = 10,
            .fg_color = "#ffffff",
            .text = "Hi",
        }
    );
    FormDesigner designer(std::move(config));
    designer.SetHold({});
    designer.SetSnap(false);
    const auto index = designer.Document().Components().size() - 1;
    EXPECT_LT(designer.VisualRect(index).w, 30.0f);
    EXPECT_TRUE(Drag(designer, {152.0f, 35.0f}, {157.0f, 35.0f}));
    EXPECT_FLOAT_EQ(FormElements::Rect(designer.Document().Components()[index])[0], 155.0f);
    EXPECT_TRUE(Drag(designer, {157.0f, 35.0f}, {152.0f, 35.0f}));
    EXPECT_FLOAT_EQ(FormElements::Rect(designer.Document().Components()[index])[0], 150.0f);
}

TEST(FormDesigner, ExtraFormsHoldTheirOwnElements)
{
    auto designer = SmallWatch();
    EXPECT_FALSE(designer.Insert(FormPart::Form, Eigen::Vector2f(100.0f, 50.0f)).has_value());
    ASSERT_EQ(designer.Document().Forms().size(), 1U);
    EXPECT_EQ(designer.SelectedForm(), 1U);
    const auto form = designer.FormBounds(1);
    EXPECT_FLOAT_EQ(form.x, 200.0f + FormDesigner::kFormGap);
    EXPECT_FALSE(Overlaps(form, designer.FormBounds(0)));

    const auto button = designer.Insert(FormPart::Button, Eigen::Vector2f(form.x + 100.0f, form.y + 60.0f));
    ASSERT_TRUE(button.has_value());
    EXPECT_EQ(FormElements::FormOf(designer.Document().Components().at(*button)), 1U);
    EXPECT_GT(designer.Document().Size()[0], 400U);

    const auto visual = designer.VisualRect(*button);
    EXPECT_TRUE(Drag(designer, {visual.x + 10.0f, visual.y + 10.0f}, {50.0f, 50.0f}));
    EXPECT_EQ(FormElements::FormOf(designer.Document().Components().at(*button)), 0U);

    const auto elements = designer.Document().Components().size();
    designer.Insert(FormPart::Panel, Eigen::Vector2f(form.x + 100.0f, form.y + 60.0f));
    designer.RemoveForm(1);
    EXPECT_TRUE(designer.Document().Forms().empty());
    EXPECT_EQ(designer.Document().Components().size(), elements);
}

TEST(FormDesigner, ExtraFormsDockAgainstTheMainForm)
{
    auto designer = SmallWatch();
    designer.Insert(FormPart::Form, std::nullopt);
    const auto form = designer.FormBounds(1);
    designer.Press({form.x + 100.0f, form.y + 60.0f});
    EXPECT_EQ(designer.SelectedForm(), 1U);
    designer.Move({form.x + 103.0f, form.y + 63.0f});
    designer.Move({form.x + 105.0f, form.y + 63.0f});
    EXPECT_TRUE(designer.Release({form.x + 105.0f, form.y + 63.0f}));
    EXPECT_FLOAT_EQ(designer.Document().Forms()[0].x, 200.0f + FormDesigner::kFormGap);
    EXPECT_FLOAT_EQ(designer.Document().Forms()[0].y, 0.0f);
}

TEST(FormDesigner, PartsLandWhereTheyAreDropped)
{
    auto designer = SmallWatch();
    const auto button = designer.Insert(FormPart::Button, Eigen::Vector2f(100.0f, 50.0f));
    ASSERT_TRUE(button.has_value());
    EXPECT_EQ(designer.Selected(), button);
    const auto& rect = FormElements::Rect(designer.Document().Components().at(*button));
    EXPECT_FLOAT_EQ(rect[0] + rect[2] / 2.0f, 100.0f);
    EXPECT_FLOAT_EQ(rect[1] + rect[3] / 2.0f, 50.0f);

    designer.Insert(FormPart::Button, Eigen::Vector2f(100.0f, 115.0f));
    EXPECT_EQ(ToolbarTexts(designer.Document()), (std::vector<std::string>{"A", "Button", "B"}));

    designer.Press({-10.0f, -10.0f});
    EXPECT_FALSE(designer.Selected().has_value());
}

TEST(FormDesigner, LayersOnlySeeTheirOwnElements)
{
    auto designer = SmallWatch();
    const Eigen::Vector2f on_button{40.0f, 30.0f};
    const Eigen::Vector2f on_panel{150.0f, 80.0f};
    EXPECT_EQ(designer.HitTest(on_button).index, 1U);
    EXPECT_EQ(designer.HitTest(on_button, 0).index, 0U);
    EXPECT_EQ(designer.HitTest(on_button, 2).index, 1U);
    EXPECT_EQ(designer.HitTest(on_button, 1).kind, FormHitKind::None);
    EXPECT_TRUE(designer.Accepts(on_panel, 0));
    EXPECT_FALSE(designer.Accepts(on_panel, 2));
    EXPECT_TRUE(designer.Accepts(on_button, 2));

    designer.Press(on_button, 0);
    EXPECT_EQ(designer.Selected(), 0U);
    designer.Release(on_button);
}

TEST(KeyboardDesigner, PlusCellsAddKeys)
{
    auto designer = SmallKeyboard();
    EXPECT_EQ(designer.HitTest(CellCenter(2.0f, 0)).kind, KeyHitKind::Cell);
    EXPECT_TRUE(Click(designer, CellCenter(2.0f, 0)));
    ASSERT_EQ(designer.Document().Keys().size(), 3U);
    ASSERT_TRUE(designer.Selected().has_value());
    const auto& key = designer.Document().Keys().at(*designer.Selected());
    EXPECT_FLOAT_EQ(key.x, 2.0f);
    EXPECT_TRUE(std::holds_alternative<BlankKey>(key.action));
}

TEST(KeyboardDesigner, ColumnsAndRowsGrowAndShrinkAfterConfirming)
{
    auto designer = SmallKeyboard();
    designer.AddColumn();
    EXPECT_FLOAT_EQ(designer.Document().Columns(), 4.0f);
    EXPECT_TRUE(designer.RemoveColumn());
    EXPECT_TRUE(designer.RemoveColumn());
    EXPECT_FLOAT_EQ(designer.Document().Columns(), 2.0f);
    EXPECT_FALSE(designer.RemoveColumn());
    EXPECT_TRUE(designer.RemoveArmed(KeyHitKind::RemoveColumn));
    EXPECT_FLOAT_EQ(designer.Document().Columns(), 2.0f);
    EXPECT_TRUE(designer.RemoveColumn());
    EXPECT_FLOAT_EQ(designer.Document().Columns(), 1.0f);
    EXPECT_EQ(designer.Document().Keys().size(), 1U);

    designer.AddRow();
    EXPECT_EQ(designer.Document().Rows(), 3);
    EXPECT_TRUE(designer.RemoveRow());
    EXPECT_TRUE(designer.RemoveRow());
    EXPECT_EQ(designer.Document().Rows(), 1);
    EXPECT_FALSE(designer.RemoveRow());
}

TEST(KeyboardDesigner, QuickClicksOnlySelectKeys)
{
    KeyboardDesigner designer(*KeyboardLayout::Parse(kKeyboardYaml));
    designer.Press(CellCenter(0.0f, 0));
    designer.Hover(CellCenter(2.0f, 1));
    EXPECT_FALSE(designer.Release(CellCenter(2.0f, 1)));
    EXPECT_EQ(designer.Selected(), 0U);
    EXPECT_FLOAT_EQ(designer.Document().Keys()[0].x, 0.0f);
    EXPECT_EQ(designer.Document().Keys()[0].row, 0);
}

TEST(KeyboardDesigner, ClickingTheSelectedKeyAgainClosesIt)
{
    auto designer = SmallKeyboard();
    EXPECT_FALSE(Click(designer, CellCenter(0.0f, 0)));
    EXPECT_EQ(designer.Selected(), 0U);
    EXPECT_FALSE(Click(designer, CellCenter(0.0f, 0)));
    EXPECT_FALSE(designer.Selected().has_value());
    EXPECT_FALSE(Click(designer, CellCenter(0.0f, 0)));
    EXPECT_EQ(designer.Selected(), 0U);
}

TEST(KeyboardDesigner, KeysDragOnlyIntoFreeSpace)
{
    auto designer = SmallKeyboard();
    designer.Press(CellCenter(0.0f, 0));
    designer.Hover(CellCenter(2.0f, 1));
    EXPECT_TRUE(designer.Release(CellCenter(2.0f, 1)));
    EXPECT_FLOAT_EQ(designer.Document().Keys().back().x, 2.0f);
    EXPECT_EQ(designer.Document().Keys().back().row, 1);

    const auto before = designer.Document().Keys().size();
    designer.Press(CellCenter(2.0f, 1));
    designer.Hover(CellCenter(1.0f, 0));
    EXPECT_FALSE(designer.Release(CellCenter(1.0f, 0)));
    EXPECT_EQ(designer.Document().Keys().size(), before);
    EXPECT_EQ(designer.Document().Keys().back().row, 1);
}

TEST(KeyboardDesigner, TemplatesGrowTheGridButNeverOverlap)
{
    auto designer = SmallKeyboard();
    EXPECT_TRUE(designer.Place(KeyTemplate::Arrows, 3.0f, 0));
    EXPECT_FLOAT_EQ(designer.Document().Columns(), 6.0f);
    EXPECT_EQ(designer.Document().Keys().size(), 6U);
    EXPECT_FALSE(designer.Place(KeyTemplate::Numpad, 0.0f, 0));
    EXPECT_EQ(designer.Document().Keys().size(), 6U);
    EXPECT_TRUE(designer.Document().Validate().has_value());
}

TEST(KeyboardDesigner, WidthStopsAtTheNextKey)
{
    auto designer = SmallKeyboard();
    EXPECT_FALSE(designer.Resize(0, kKeyStep));
    EXPECT_TRUE(designer.Resize(1, 1.0f));
    EXPECT_FLOAT_EQ(designer.Document().Keys()[1].width, 2.0f);
    EXPECT_FALSE(designer.Resize(1, kKeyStep));
    EXPECT_TRUE(designer.Duplicate(0));
    EXPECT_EQ(designer.Document().Keys().size(), 3U);
    EXPECT_EQ(designer.Document().Keys().back().row, 1);
}
