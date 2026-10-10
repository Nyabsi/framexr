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

#include <algorithm>
#include <chrono>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Modular.hpp"
#include "OverlayContainer.hpp"
#include "RecordingBackend.hpp"
#include "State.hpp"
#include "TestApp.hpp"
#include "Yaml.hpp"

namespace {

constexpr VkExtent2D kPanelExtent{200, 100};

class Interaction : public testing::Test {
  protected:
    auto SetUp() -> void override {
        app_ = &TestApp::Get();
        app_->input = InputState();
        app_->config.transform_values.clear();
        app_->config.curve_values.clear();
        app_->config.allow_sliding = true;
        app_->anchor = Eigen::Affine3f::Identity();
        overlays_ = OverlayContainer::CreateEmpty();
        AimAway(1);
    }

    auto AddPanel(std::string name, const Eigen::Vector3f& position, std::shared_ptr<std::vector<RecordedEvent>> events)
        -> OverlayId {
        OverlayState state;
        state.name = std::move(name);
        state.want_visible = true;
        state.interactable = true;
        state.grabbable = true;
        state.anchored = true;
        state.transform = Eigen::Translation3f(position) * Eigen::Affine3f::Identity();
        state.interaction_transform = OverlayState::UiTransform(kPanelExtent);
        const auto id = state.id;
        overlays_->Add(
            std::make_unique<Overlay>(std::move(state), std::make_unique<RecordingBackend>(kPanelExtent, std::move(events)))
        );
        return id;
    }

    auto Aim(size_t pointer, const Eigen::Vector3f& origin) -> void {
        app_->input.pointers[pointer].pose = Eigen::Translation3f(origin) * Eigen::Affine3f::Identity();
    }

    auto AimAway(size_t pointer) -> void {
        app_->input.pointers[pointer].pose = Eigen::Affine3f(Eigen::AngleAxisf(std::numbers::pi_v<float>, Eigen::Vector3f::UnitY()));
    }

    auto Frame(const PointerState& left, const PointerState& right = {}) -> std::array<InteractionResult, 2> {
        app_->input.PreUpdate();
        app_->input.pointers[0].now = left;
        app_->input.pointers[1].now = right;
        app_->input.PostUpdate(app_->config);
        return InputState::Interact(*app_, *overlays_);
    }

    auto Pointer(size_t index) -> ::Pointer& { return app_->input.pointers[index]; }

    AppState* app_ = nullptr;
    std::unique_ptr<OverlayContainer> overlays_;
};

auto Events(const std::shared_ptr<std::vector<RecordedEvent>>& events, RecordedKind kind) -> std::vector<RecordedEvent> {
    std::vector<RecordedEvent> filtered;
    std::ranges::copy_if(*events, std::back_inserter(filtered), [kind](const RecordedEvent& event) -> bool {
        return event.kind == kind;
    });
    return filtered;
}

} // namespace

TEST_F(Interaction, HoverReportsUvThroughInteractionTransform) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);

    Aim(0, Eigen::Vector3f::Zero());
    const auto center = Frame({});
    EXPECT_FLOAT_EQ(center[0].length, 2.0f);
    EXPECT_EQ(Pointer(0).interaction.hovered_id, id);

    Aim(0, {0.25f, 0.1f, 0.0f});
    Frame({});

    const auto hovers = Events(events, RecordedKind::Hover);
    ASSERT_EQ(hovers.size(), 2U);
    EXPECT_NEAR(hovers[0].uv.x(), 0.5f, 1e-5f);
    EXPECT_NEAR(hovers[0].uv.y(), 0.5f, 1e-5f);
    EXPECT_TRUE(hovers[0].primary);
    EXPECT_NEAR(hovers[1].uv.x(), 0.75f, 1e-5f);
    EXPECT_NEAR(hovers[1].uv.y(), 0.3f, 1e-5f);
}

TEST_F(Interaction, HitsOutsideUvRangeAreIgnored) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);

    Aim(0, {0.6f, 0.0f, 0.0f});
    const auto result = Frame({});
    EXPECT_EQ(result[0].length, 0.0f);
    EXPECT_FALSE(Pointer(0).interaction.hovered_id.has_value());
    EXPECT_TRUE(events->empty());
}

TEST_F(Interaction, ClickPressAndReleaseAreDelivered) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());

    Frame({});
    Frame({.click = true});
    EXPECT_EQ(Pointer(0).interaction.clicked_id, id);
    Frame({});
    EXPECT_FALSE(Pointer(0).interaction.clicked_id.has_value());

    const auto clicks = Events(events, RecordedKind::Pointer);
    ASSERT_EQ(clicks.size(), 2U);
    EXPECT_TRUE(clicks[0].pressed);
    EXPECT_FALSE(clicks[1].pressed);
    EXPECT_EQ(clicks[0].mode, PointerMode::Left);
}

TEST_F(Interaction, ModifiersSelectPointerMode) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());

    Frame({.click = true, .click_modifier_right = true});
    Frame({.click_modifier_right = true});
    Frame({.click = true, .click_modifier_middle = true});

    const auto clicks = Events(events, RecordedKind::Pointer);
    ASSERT_EQ(clicks.size(), 3U);
    EXPECT_EQ(clicks[0].mode, PointerMode::Right);
    EXPECT_EQ(clicks[2].mode, PointerMode::Middle);
}

TEST_F(Interaction, ReleaseGoesToTheOverlayThatWasPressed) {
    auto first = std::make_shared<std::vector<RecordedEvent>>();
    auto second = std::make_shared<std::vector<RecordedEvent>>();
    AddPanel("first", {0.0f, 0.0f, -2.0f}, first);
    AddPanel("second", {3.0f, 0.0f, -2.0f}, second);

    Aim(0, Eigen::Vector3f::Zero());
    Frame({.click = true});
    Aim(0, {3.0f, 0.0f, 0.0f});
    Frame({});

    const auto first_clicks = Events(first, RecordedKind::Pointer);
    ASSERT_EQ(first_clicks.size(), 2U);
    EXPECT_FALSE(first_clicks[1].pressed);
    EXPECT_TRUE(Events(second, RecordedKind::Pointer).empty());
    EXPECT_EQ(Events(first, RecordedKind::Left).size(), 1U);
}

TEST_F(Interaction, ReleaseIntoEmptySpaceStillReleases) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);

    Aim(0, Eigen::Vector3f::Zero());
    Frame({.click = true});
    AimAway(0);
    const auto result = Frame({});

    EXPECT_EQ(result[0].length, 0.0f);
    EXPECT_EQ(Events(events, RecordedKind::Left).size(), 1U);
    const auto clicks = Events(events, RecordedKind::Pointer);
    ASSERT_EQ(clicks.size(), 2U);
    EXPECT_FALSE(clicks[1].pressed);
}

TEST_F(Interaction, ScrollIsForwarded) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());

    Frame({.scroll_x = 0.05f, .scroll_y = 0.05f});
    Frame({.scroll_x = 0.5f, .scroll_y = -1.0f});

    const auto scrolls = Events(events, RecordedKind::Scroll);
    ASSERT_EQ(scrolls.size(), 1U);
    EXPECT_FLOAT_EQ(scrolls[0].delta_y, -1.0f);
    EXPECT_FLOAT_EQ(scrolls[0].delta_x, 0.5f);
}

TEST_F(Interaction, LeftPointerFirstKeepsPrimaryForItself) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());
    Aim(1, {0.1f, 0.0f, 0.0f});
    Pointer(0).last_click = std::chrono::steady_clock::now();
    Pointer(1).last_click = Pointer(0).last_click - std::chrono::seconds(1);

    Frame({});

    const auto hovers = Events(events, RecordedKind::Hover);
    ASSERT_EQ(hovers.size(), 2U);
    EXPECT_EQ(hovers[0].pointer, 0U);
    EXPECT_TRUE(hovers[0].primary);
    EXPECT_EQ(hovers[1].pointer, 1U);
    EXPECT_FALSE(hovers[1].primary);
}

TEST_F(Interaction, RecentlyClickedRightPointerGoesFirstAndLowerIndexTakesPrimary) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());
    Aim(1, {0.1f, 0.0f, 0.0f});
    Pointer(1).last_click = std::chrono::steady_clock::now();
    Pointer(0).last_click = Pointer(1).last_click - std::chrono::seconds(1);

    Frame({});

    const auto hovers = Events(events, RecordedKind::Hover);
    ASSERT_EQ(hovers.size(), 2U);
    EXPECT_EQ(hovers[0].pointer, 1U);
    EXPECT_TRUE(hovers[0].primary);
    EXPECT_EQ(hovers[1].pointer, 0U);
    EXPECT_TRUE(hovers[1].primary);
    EXPECT_EQ(overlays_->ById(id)->primary_pointer, 0U);
}

TEST_F(Interaction, GrabMovesRealignsAndSavesOverlay) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());

    const auto grab = Frame({.grab = true});
    ASSERT_TRUE(Pointer(0).interaction.grabbed.has_value());
    EXPECT_EQ(Pointer(0).interaction.grabbed->grabbed_id, id);
    EXPECT_TRUE(Pointer(0).interaction.grabbed->offset.isApprox(Eigen::Vector3f(0.0f, 0.0f, -2.0f)));
    ASSERT_TRUE(grab[0].haptics.has_value());
    EXPECT_FLOAT_EQ(grab[0].haptics->intensity, 0.25f);
    EXPECT_FLOAT_EQ(grab[0].haptics->duration, 0.1f);
    EXPECT_FLOAT_EQ(grab[0].haptics->frequency, 0.1f);
    EXPECT_TRUE(Events(events, RecordedKind::Hover).empty());

    Aim(0, {0.5f, 0.0f, 0.0f});
    const auto held = Frame({.grab = true});
    EXPECT_FLOAT_EQ(held[0].length, 0.1f);
    auto* overlay = overlays_->ById(id);
    ASSERT_NE(overlay, nullptr);
    EXPECT_TRUE(overlay->state.transform.translation().isApprox(Eigen::Vector3f(0.5f, 0.0f, -2.0f)));
    const Eigen::Matrix3f linear = overlay->state.transform.linear();
    const Eigen::Vector3f facing = linear.col(2).normalized();
    EXPECT_TRUE(facing.isApprox(-Eigen::Vector3f(0.5f, 0.0f, -2.0f).normalized(), 1e-4f));
    EXPECT_NEAR(linear.col(1).normalized().dot(Eigen::Vector3f::UnitY()), 1.0f, 1e-4f);

    Frame({});
    EXPECT_FALSE(Pointer(0).interaction.grabbed.has_value());
    ASSERT_TRUE(overlay->state.saved_transform.has_value());
    const auto saved = std::ranges::find(
        app_->config.transform_values,
        std::string("panel"),
        &std::pair<std::string, Eigen::Affine3f>::first
    );
    ASSERT_NE(saved, app_->config.transform_values.end());
    EXPECT_TRUE(saved->second.isApprox(overlay->state.transform, 1e-5f));
}

TEST_F(Interaction, GrabWithClickScalesAndGrabWithScrollSlides) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());

    Frame({.grab = true});
    Frame({.scroll_y = 1.0f, .click = true, .grab = true});
    auto* overlay = overlays_->ById(id);
    EXPECT_NEAR(overlay->state.transform.linear().col(0).norm(), 0.975f, 1e-5f);
    EXPECT_EQ(Pointer(0).interaction.mode, PointerMode::Special);

    Frame({.scroll_y = 2.0f, .grab = true});
    EXPECT_NEAR(Pointer(0).interaction.grabbed->offset.z(), -2.1f, 1e-5f);
    EXPECT_NEAR(overlay->state.transform.translation().z(), -2.1f, 1e-5f);
}

TEST_F(Interaction, ScrollWithOtherHandWhileGrabbedCurves) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());
    Aim(1, {0.1f, 0.0f, 0.0f});

    Frame({.grab = true});
    Frame({.grab = true}, {.scroll_y = -10.0f});

    auto* overlay = overlays_->ById(id);
    ASSERT_TRUE(overlay->state.curvature.has_value());
    EXPECT_NEAR(*overlay->state.curvature, 0.1f, 1e-5f);
    EXPECT_TRUE(Events(events, RecordedKind::Scroll).empty());

    Frame({.grab = true}, {.scroll_y = 20.0f});
    EXPECT_FALSE(overlay->state.curvature.has_value());

    Frame({}, {});
    EXPECT_TRUE(std::ranges::none_of(app_->config.curve_values, [](const auto& entry) -> bool { return entry.first == "panel"; }));
}

TEST_F(Interaction, ModularButtonClickRunsItsAction) {
    const auto parsed = Yaml::Parse(R"(
width: 0.3
size: [200, 100]
elements:
  - type: Button
    rect: [0, 0, 200, 100]
    font_size: 14
    fg_color: "#ffffff"
    bg_color: "#000000"
    text: "Toggle"
    click_up:
      - type: System
        action: ToggleNotifications
)");
    ASSERT_TRUE(parsed.has_value());
    const auto config = parsed->get<ModularUiConfig>();
    auto canvas = Modular::CreateCanvas(config.size, config.elements, *app_);
    ASSERT_TRUE(canvas.has_value());

    OverlayState state;
    state.name = "buttons";
    state.want_visible = true;
    state.interactable = true;
    state.transform = Eigen::Translation3f(0.0f, 0.0f, -1.0f) * Eigen::Affine3f::Identity();
    state.interaction_transform = OverlayState::UiTransform(kPanelExtent);
    overlays_->Add(std::make_unique<Overlay>(std::move(state), std::move(*canvas)));

    const bool before = app_->config.notifications_enabled;
    Aim(0, Eigen::Vector3f::Zero());
    Frame({});
    Frame({.click = true});
    EXPECT_EQ(app_->config.notifications_enabled, before);
    Frame({});
    EXPECT_EQ(app_->config.notifications_enabled, !before);
    app_->config.notifications_enabled = before;
}
