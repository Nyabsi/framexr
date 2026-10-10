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
#include <array>
#include <chrono>
#include <deque>
#include <format>
#include <memory>
#include <numbers>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "Keyboard.hpp"
#include "Modular.hpp"
#include "OverlayContainer.hpp"
#include "OverlayLayout.hpp"
#include "RecordingBackend.hpp"
#include "Screen.hpp"
#include "State.hpp"
#include "TestApp.hpp"
#include "Yaml.hpp"

namespace {

constexpr VkExtent2D kPanelExtent{200, 100};
constexpr float kFrameSeconds = 1.0f / 90.0f;

class Interaction : public testing::Test {
  protected:
    auto SetUp() -> void override {
        app_ = &TestApp::Get();
        app_->input = InputState();
        app_->config.transform_values.clear();
        app_->config.curve_values.clear();
        app_->config.allow_sliding = true;
        app_->config.grab_smoothing = false;
        app_->screens.clear();
        app_->input.hmd = Eigen::Affine3f::Identity();
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
        state.recenter = true;
        state.show_hide = true;
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
        app_->input.delta_seconds = kFrameSeconds;
        app_->input.pointers[0].now = left;
        app_->input.pointers[1].now = right;
        app_->input.PostUpdate(app_->config);
        return InputState::Interact(*app_, *overlays_);
    }

    auto Pointer(size_t index) -> ::Pointer& { return app_->input.pointers[index]; }

    auto RunTasks() -> void {
        for (int round = 0; round < 4; ++round) {
            std::deque<Task> due;
            app_->tasks.TakeDue(due);
            for (auto& task : due) {
                std::visit(
                    [&](auto& value) -> void {
                        using TaskType = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<TaskType, ModifyOverlayTask>) {
                            if (auto* overlay = overlays_->BySelector(value.selector)) {
                                value.modify(*app_, overlay->state);
                            }
                        } else if constexpr (std::is_same_v<TaskType, DropOverlayTask>) {
                            overlays_->Remove(value.selector);
                        } else if constexpr (std::is_same_v<TaskType, CreateOverlayTask>) {
                            if (overlays_->BySelector(value.selector) == nullptr) {
                                if (auto made = value.create(*app_)) {
                                    overlays_->Add(std::make_unique<Overlay>(std::move(made->first), std::move(made->second)));
                                }
                            }
                        }
                    },
                    task
                );
            }
        }
    }

    auto Bounds(OverlayId id) -> ViewRect {
        const auto corners = OverlayLayout::Corners(*overlays_->ById(id));
        EXPECT_TRUE(corners.has_value());
        return corners ? OverlayLayout::ViewBounds(*corners, app_->input.hmd) : ViewRect{};
    }

    AppState* app_ = nullptr;
    std::unique_ptr<OverlayContainer> overlays_;
};

auto Pose(const Eigen::Vector3f& position, const Eigen::Quaternionf& rotation) -> Eigen::Affine3f {
    Eigen::Affine3f pose = Eigen::Affine3f::Identity();
    pose.linear() = rotation.toRotationMatrix();
    pose.translation() = position;
    return pose;
}

auto Compose(const Eigen::Affine3f& first, const Eigen::Affine3f& second) -> Eigen::Affine3f {
    return Eigen::Affine3f(first.matrix() * second.matrix());
}

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

TEST_F(Interaction, GrabFollowsControllerRotationFreely) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());

    const auto grab = Frame({.grab = true});
    ASSERT_TRUE(Pointer(0).interaction.grabbed.has_value());
    EXPECT_EQ(Pointer(0).interaction.grabbed->grabbed_id, id);
    ASSERT_TRUE(grab[0].haptics.has_value());
    EXPECT_FLOAT_EQ(grab[0].haptics->intensity, 0.25f);
    EXPECT_TRUE(Events(events, RecordedKind::Hover).empty());

    const Eigen::Affine3f turned = Pose(
        Eigen::Vector3f(0.5f, 0.2f, 0.0f),
        Eigen::Quaternionf(Eigen::AngleAxisf(0.7f, Eigen::Vector3f::UnitY()))
            * Eigen::Quaternionf(Eigen::AngleAxisf(0.3f, Eigen::Vector3f::UnitX()))
    );
    Pointer(0).pose = turned;
    const auto held = Frame({.grab = true});
    EXPECT_FLOAT_EQ(held[0].length, 0.1f);

    const auto* overlay = overlays_->ById(id);
    ASSERT_NE(overlay, nullptr);
    const Eigen::Affine3f expected = Compose(turned, Eigen::Affine3f(Eigen::Translation3f(0.0f, 0.0f, -2.0f)));
    EXPECT_TRUE(overlay->state.transform.matrix().isApprox(expected.matrix(), 1e-4f));
}

TEST_F(Interaction, DropRemembersPositionRelativeToHead) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());
    app_->input.hmd = Pose(Eigen::Vector3f(0.0f, 1.6f, 0.0f), Eigen::Quaternionf(Eigen::AngleAxisf(0.4f, Eigen::Vector3f::UnitY())));

    Frame({.grab = true});
    Aim(0, {0.3f, 0.0f, 0.0f});
    Frame({.grab = true});
    Frame({});

    const auto* overlay = overlays_->ById(id);
    ASSERT_TRUE(overlay->state.saved_transform.has_value());
    const Eigen::Affine3f expected = Compose(Eigen::Affine3f(app_->input.hmd.matrix().inverse()), overlay->state.transform);
    EXPECT_TRUE(overlay->state.saved_transform->matrix().isApprox(expected.matrix(), 1e-4f));
    const auto saved = std::ranges::find(
        app_->config.transform_values,
        std::string("panel"),
        &std::pair<std::string, Eigen::Affine3f>::first
    );
    ASSERT_NE(saved, app_->config.transform_values.end());
    EXPECT_TRUE(saved->second.matrix().isApprox(expected.matrix(), 1e-4f));
}

TEST_F(Interaction, StickPushesAwayAndScalesWithDistance) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());

    Frame({.grab = true});
    Frame({.scroll_y = 1.0f, .grab = true});
    const auto* overlay = overlays_->ById(id);
    const Eigen::Matrix3f pushed = overlay->state.transform.linear();
    EXPECT_NEAR(overlay->state.transform.translation().z(), -2.05f, 1e-5f);
    EXPECT_NEAR(pushed.col(0).norm(), 1.025f, 1e-5f);

    Frame({.scroll_y = -1.0f, .grab = true});
    Frame({.scroll_y = -1.0f, .click = true, .grab = true});
    const Eigen::Matrix3f pulled = overlay->state.transform.linear();
    EXPECT_NEAR(overlay->state.transform.translation().z(), -1.95f, 1e-5f);
    EXPECT_NEAR(pulled.col(0).norm(), 0.975f, 1e-5f);
    EXPECT_NE(Pointer(0).interaction.mode, PointerMode::Special);
}

TEST_F(Interaction, StickScalesWithDistanceFromTheHead) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, {0.0f, 0.0f, -0.5f});

    Frame({.grab = true});
    Frame({.scroll_y = 1.0f, .grab = true});
    const auto* overlay = overlays_->ById(id);
    const Eigen::Matrix3f pushed = overlay->state.transform.linear();
    EXPECT_NEAR(overlay->state.transform.translation().z(), -2.05f, 1e-5f);
    EXPECT_NEAR(pushed.col(0).norm(), 2.05f / 2.0f, 1e-5f);
}

TEST_F(Interaction, StickDriftDoesNotMoveOrResize) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());

    Frame({.grab = true});
    for (int frame = 0; frame < 100; ++frame) {
        Frame({.scroll_y = frame % 2 == 0 ? 0.15f : -0.1f, .grab = true});
    }
    const auto* overlay = overlays_->ById(id);
    const Eigen::Matrix3f linear = overlay->state.transform.linear();
    EXPECT_TRUE(overlay->state.transform.translation().isApprox(Eigen::Vector3f(0.0f, 0.0f, -2.0f), 1e-6f));
    EXPECT_NEAR(linear.col(0).norm(), 1.0f, 1e-6f);
}

TEST_F(Interaction, GrabSmoothingRemovesHandTremor) {
    app_->config.grab_smoothing = true;
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());
    Frame({.grab = true});

    float wobble = 0.0f;
    for (int frame = 0; frame < 90; ++frame) {
        Aim(0, {frame % 2 == 0 ? 0.002f : -0.002f, 0.0f, 0.0f});
        Frame({.grab = true});
        if (frame > 30) {
            wobble = std::max(wobble, std::abs(overlays_->ById(id)->state.transform.translation().x()));
        }
    }
    EXPECT_LT(wobble, 0.0005f);
}

TEST_F(Interaction, GrabSmoothingKeepsUpWithDeliberateMoves) {
    app_->config.grab_smoothing = true;
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());
    Frame({.grab = true});

    for (int frame = 1; frame <= 30; ++frame) {
        Aim(0, {0.5f * static_cast<float>(frame) / 30.0f, 0.0f, 0.0f});
        Frame({.grab = true});
    }
    const float lag = 0.5f - overlays_->ById(id)->state.transform.translation().x();
    EXPECT_LT(lag, 0.05f);
    for (int frame = 0; frame < 30; ++frame) {
        Frame({.grab = true});
    }
    EXPECT_NEAR(overlays_->ById(id)->state.transform.translation().x(), 0.5f, 0.001f);
}

TEST_F(Interaction, ShowPlacesOverlaysRelativeToCurrentHead) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -1.0f}, events);
    auto* overlay = overlays_->ById(id);
    overlay->state.want_visible = false;
    overlay->state.saved_transform = Eigen::Affine3f(Eigen::Translation3f(0.2f, -0.1f, -1.5f));

    app_->input.hmd = Eigen::Translation3f(0.0f, 1.6f, 0.0f) * Eigen::Affine3f::Identity();
    overlays_->ShowHide(*app_);
    ASSERT_TRUE(overlay->state.want_visible);
    EXPECT_TRUE(overlay->state.transform.translation().isApprox(Eigen::Vector3f(0.2f, 1.5f, -1.5f), 1e-5f));

    overlays_->ShowHide(*app_);
    EXPECT_FALSE(overlay->state.want_visible);

    const Eigen::Affine3f turned = Pose(
        Eigen::Vector3f(1.0f, 1.6f, 2.0f),
        Eigen::Quaternionf(Eigen::AngleAxisf(std::numbers::pi_v<float> / 2.0f, Eigen::Vector3f::UnitY()))
    );
    app_->input.hmd = turned;
    overlays_->ShowHide(*app_);
    const Eigen::Affine3f expected = Compose(turned, *overlay->state.saved_transform);
    EXPECT_TRUE(overlay->state.transform.matrix().isApprox(expected.matrix(), 1e-5f));
    const Eigen::Vector3f to_overlay = overlay->state.transform.translation() - turned.translation();
    EXPECT_NEAR(to_overlay.norm(), Eigen::Vector3f(0.2f, -0.1f, -1.5f).norm(), 1e-5f);
    const Eigen::Matrix3f overlay_linear = overlay->state.transform.linear();
    const Eigen::Matrix3f head_linear = turned.linear();
    EXPECT_TRUE(overlay_linear.col(2).isApprox(head_linear.col(2), 1e-5f));
}

TEST_F(Interaction, ShowWhileLyingDownPlacesOverlayWhereYouLook) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -1.0f}, events);
    auto* overlay = overlays_->ById(id);
    overlay->state.want_visible = false;
    overlay->state.saved_transform = Eigen::Affine3f(Eigen::Translation3f(0.0f, 0.0f, -1.0f));

    app_->input.hmd = Pose(
        Eigen::Vector3f(0.0f, 0.5f, 0.0f),
        Eigen::Quaternionf(Eigen::AngleAxisf(std::numbers::pi_v<float> / 2.0f, Eigen::Vector3f::UnitX()))
    );
    overlays_->ShowHide(*app_);
    EXPECT_TRUE(overlay->state.transform.translation().isApprox(Eigen::Vector3f(0.0f, 1.5f, 0.0f), 1e-5f));
}

TEST_F(Interaction, RecenterMovesOnlyVisibleOverlays) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto shown = AddPanel("shown", {0.0f, 0.0f, -1.0f}, events);
    const auto hidden = AddPanel("hidden", {5.0f, 0.0f, -1.0f}, events);
    overlays_->ById(hidden)->state.want_visible = false;
    for (const auto id : {shown, hidden}) {
        overlays_->ById(id)->state.saved_transform = Eigen::Affine3f(Eigen::Translation3f(0.0f, 0.0f, -1.0f));
    }

    app_->input.hmd = Eigen::Translation3f(3.0f, 1.0f, 0.0f) * Eigen::Affine3f::Identity();
    overlays_->Recenter(*app_);
    EXPECT_TRUE(overlays_->ById(shown)->state.transform.translation().isApprox(Eigen::Vector3f(3.0f, 1.0f, -1.0f), 1e-5f));
    EXPECT_TRUE(overlays_->ById(hidden)->state.transform.translation().isApprox(Eigen::Vector3f(5.0f, 0.0f, -1.0f), 1e-5f));
}

TEST_F(Interaction, DefaultLayoutArcsScreensAroundTheHead) {
    std::vector<ScreenOverlay> screens(3);
    const std::array<int32_t, 3> positions{1920, 0, 3840};
    for (size_t i = 0; i < screens.size(); ++i) {
        screens[i].state.name = std::format("screen{}", i);
        screens[i].state.spawn_scale = 1.5f;
        screens[i].desktop_position = {positions[i], 0};
    }
    Screens::ArrangeInFrontOfHead(screens);

    const auto& middle = screens[0].state;
    EXPECT_NEAR(middle.spawn_point.x(), 0.0f, 1e-5f);
    EXPECT_LT(middle.spawn_point.y(), -0.1f);
    EXPECT_LT(middle.spawn_point.z(), -0.9f);
    EXPECT_LT(screens[1].state.spawn_point.x(), -0.5f);
    EXPECT_GT(screens[2].state.spawn_point.x(), 0.5f);
    for (const auto& screen : screens) {
        EXPECT_NEAR(screen.state.spawn_point.norm(), 1.0f, 1e-5f);
        const Eigen::Vector3f facing = screen.state.spawn_rotation * Eigen::Vector3f::UnitZ();
        EXPECT_TRUE(facing.isApprox(-screen.state.spawn_point.normalized(), 1e-4f)) << screen.state.name;
    }
}

TEST_F(Interaction, KeyboardSpawnsBelowViewFacingTheHead) {
    auto keyboard = Keyboard::Create(*app_, nullptr);
    ASSERT_TRUE(keyboard.has_value());
    const auto& state = (*keyboard)->state;
    EXPECT_FLOAT_EQ(state.spawn_point.x(), 0.0f);
    EXPECT_LT(state.spawn_point.y(), -0.3f);
    EXPECT_LT(state.spawn_point.z(), 0.0f);
    const Eigen::Vector3f facing = state.spawn_rotation * Eigen::Vector3f::UnitZ();
    EXPECT_TRUE(facing.isApprox(-state.spawn_point.normalized(), 1e-4f));
}

TEST_F(Interaction, KeyboardUsesTheConfiguredProfile) {
    app_->config.keyboard_profile = "full-size";
    auto keyboard = Keyboard::Create(*app_, nullptr);
    app_->config.keyboard_profile = "keychron-v1-ansi";
    ASSERT_TRUE(keyboard.has_value());
    const auto meta = (*keyboard)->backend->Meta();
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->extent.width, (23U * 80U) + 28U);

    auto compact = Keyboard::Create(*app_, nullptr);
    ASSERT_TRUE(compact.has_value());
    EXPECT_NEAR((*compact)->state.spawn_scale * 23.35f, (*keyboard)->state.spawn_scale * 16.35f, 1e-4f);
}

TEST_F(Interaction, ProfileKeySwapsTheKeyboardInPlace) {
    auto created = Keyboard::Create(*app_, nullptr);
    ASSERT_TRUE(created.has_value());
    auto& original = (*created)->state;
    original.want_visible = true;
    original.show_hide = true;
    original.transform = Pose(Eigen::Vector3f(0.2f, -0.4f, -0.9f), Eigen::Quaternionf::Identity());
    original.transform.linear() *= original.spawn_scale;
    original.saved_transform = original.transform;
    const OverlayState before = original;
    overlays_->Add(std::move(*created));
    app_->config.transform_values = {{std::string(kKeyboardName), *before.saved_transform}};

    Keyboard::SwitchProfile(*app_, nullptr, "full-size");
    EXPECT_EQ(app_->config.keyboard_profile, "full-size");
    RunTasks();

    auto* swapped = overlays_->ByName(kKeyboardName);
    ASSERT_NE(swapped, nullptr);
    EXPECT_NE(swapped->state.id, before.id);
    EXPECT_TRUE(swapped->state.want_visible);
    EXPECT_TRUE(swapped->state.show_hide);
    EXPECT_EQ(swapped->backend->Meta()->extent.width, (23U * 80U) + 28U);
    const float factor = 23.35f / 16.35f;
    const Eigen::Matrix3f linear = swapped->state.transform.linear();
    const Eigen::Matrix3f linear_before = before.transform.linear();
    EXPECT_TRUE(swapped->state.transform.translation().isApprox(before.transform.translation(), 1e-6f));
    EXPECT_NEAR(linear.col(0).norm(), linear_before.col(0).norm() * factor, 1e-5f);
    ASSERT_TRUE(swapped->state.saved_transform.has_value());
    EXPECT_TRUE(app_->config.transform_values.front().second.matrix().isApprox(swapped->state.saved_transform->matrix()));
    app_->config.keyboard_profile = "keychron-v1-ansi";
}

TEST_F(Interaction, DroppingOntoAnotherOverlayKeepsTheOverlap) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto fixed = AddPanel("fixed", {0.0f, 0.0f, -2.0f}, events);
    const auto moving = AddPanel("moving", {0.3f, 0.0f, -2.0f}, events);

    Aim(0, {0.6f, 0.0f, 0.0f});
    Frame({.grab = true});
    ASSERT_TRUE(Pointer(0).interaction.grabbed.has_value());
    Frame({});

    EXPECT_TRUE(overlays_->ById(moving)->state.transform.translation().isApprox(Eigen::Vector3f(0.3f, 0.0f, -2.0f), 1e-6f));
    EXPECT_TRUE(Bounds(moving).Overlaps(Bounds(fixed), 0.0f));
}

TEST_F(Interaction, ShowHideKeepsOverlapsTheUserMade) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto screen = AddPanel("screen", {0.0f, 0.0f, -1.0f}, events);
    const auto keyboard = AddPanel(std::string(kKeyboardName), {0.0f, -0.3f, -1.0f}, events);
    app_->screens.push_back(ScreenMeta{.name = "screen", .id = screen});
    overlays_->ById(screen)->state.saved_transform = overlays_->ById(screen)->state.transform;
    ASSERT_TRUE(Bounds(keyboard).Overlaps(Bounds(screen), 0.0f));

    Aim(0, {0.0f, -0.45f, 0.0f});
    Frame({.grab = true});
    ASSERT_EQ(Pointer(0).interaction.grabbed->grabbed_id, keyboard);
    Frame({});

    overlays_->ShowHide(*app_);
    app_->input.hmd = Eigen::Translation3f(0.0f, 1.6f, 0.0f) * Eigen::Affine3f::Identity();
    overlays_->ShowHide(*app_);
    ASSERT_TRUE(overlays_->ById(keyboard)->state.want_visible);
    EXPECT_TRUE(overlays_->ById(keyboard)->state.transform.translation().isApprox(Eigen::Vector3f(0.0f, 1.3f, -1.0f), 1e-5f));
    EXPECT_TRUE(Bounds(keyboard).Overlaps(Bounds(screen), 0.0f));
}

TEST_F(Interaction, ShowHideClearsOverlapsTheUserDidNotMake) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto screen = AddPanel("screen", {0.0f, 0.0f, -1.0f}, events);
    const auto keyboard = AddPanel(std::string(kKeyboardName), {0.0f, -0.3f, -1.0f}, events);
    app_->screens.push_back(ScreenMeta{.name = "screen", .id = screen});
    for (const auto id : {screen, keyboard}) {
        auto& state = overlays_->ById(id)->state;
        state.saved_transform = state.transform;
    }
    ASSERT_TRUE(Bounds(keyboard).Overlaps(Bounds(screen), 0.0f));

    overlays_->ShowHide(*app_);
    overlays_->ShowHide(*app_);
    ASSERT_TRUE(overlays_->ById(keyboard)->state.want_visible);
    EXPECT_TRUE(overlays_->ById(screen)->state.transform.translation().isApprox(Eigen::Vector3f(0.0f, 0.0f, -1.0f), 1e-6f));
    EXPECT_FALSE(Bounds(keyboard).Overlaps(Bounds(screen), 0.0f));
}

TEST_F(Interaction, DraggingAwayForgetsTheOverlap) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto screen = AddPanel("screen", {0.0f, 0.0f, -1.0f}, events);
    const auto keyboard = AddPanel(std::string(kKeyboardName), {0.0f, -0.3f, -1.0f}, events);
    app_->screens.push_back(ScreenMeta{.name = "screen", .id = screen});
    overlays_->ById(screen)->state.saved_transform = overlays_->ById(screen)->state.transform;

    Aim(0, {0.0f, -0.45f, 0.0f});
    Frame({.grab = true});
    Frame({});
    Frame({.grab = true});
    Aim(0, {0.0f, -0.95f, 0.0f});
    Frame({.grab = true});
    Frame({});
    ASSERT_FALSE(Bounds(keyboard).Overlaps(Bounds(screen), 0.0f));

    overlays_->ById(keyboard)->state.saved_transform = Eigen::Affine3f(Eigen::Translation3f(0.0f, -0.3f, -1.0f));
    overlays_->Recenter(*app_);
    EXPECT_FALSE(Bounds(keyboard).Overlaps(Bounds(screen), 0.0f));
}

TEST_F(Interaction, RecenterClearsOverlapsThatWereNotThereBefore) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto screen = AddPanel("screen", {0.0f, 0.0f, -1.0f}, events);
    const auto keyboard = AddPanel(std::string(kKeyboardName), {0.0f, -1.0f, -1.0f}, events);
    app_->screens.push_back(ScreenMeta{.name = "screen", .id = screen});
    overlays_->ById(screen)->state.saved_transform = Eigen::Affine3f(Eigen::Translation3f(0.0f, 0.0f, -1.0f));
    overlays_->ById(keyboard)->state.saved_transform = Eigen::Affine3f(Eigen::Translation3f(0.0f, -0.3f, -1.0f));
    ASSERT_FALSE(Bounds(keyboard).Overlaps(Bounds(screen), 0.0f));

    overlays_->Recenter(*app_);
    EXPECT_TRUE(overlays_->ById(screen)->state.transform.translation().isApprox(Eigen::Vector3f(0.0f, 0.0f, -1.0f), 1e-6f));
    EXPECT_FALSE(Bounds(keyboard).Overlaps(Bounds(screen), 0.0f));
    EXPECT_NEAR(overlays_->ById(keyboard)->state.transform.translation().norm(), Eigen::Vector3f(0.0f, -0.3f, -1.0f).norm(), 1e-4f);
}

TEST_F(Interaction, ShowKeepsKeyboardClearOfScreen) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto screen = AddPanel("screen", {0.0f, 0.0f, -1.0f}, events);
    const auto keyboard = AddPanel(std::string(kKeyboardName), {0.0f, 0.0f, -1.0f}, events);
    app_->screens.push_back(ScreenMeta{.name = "screen", .id = screen});
    const Eigen::Affine3f screen_spot(Eigen::Translation3f(0.0f, -0.1f, -1.0f));
    for (const auto id : {screen, keyboard}) {
        overlays_->ById(id)->state.want_visible = false;
    }
    overlays_->ById(screen)->state.saved_transform = screen_spot;
    overlays_->ById(keyboard)->state.saved_transform = Eigen::Affine3f(Eigen::Translation3f(0.0f, -0.4f, -1.0f));

    overlays_->ShowHide(*app_);
    ASSERT_TRUE(overlays_->ById(keyboard)->state.want_visible);
    EXPECT_TRUE(overlays_->ById(screen)->state.transform.matrix().isApprox(screen_spot.matrix(), 1e-6f));
    EXPECT_FALSE(Bounds(keyboard).Overlaps(Bounds(screen), 0.0f));
    EXPECT_LT(Bounds(keyboard).max_elevation, Bounds(screen).min_elevation);
}

TEST_F(Interaction, RecenterStepsAroundLockedOverlays) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto locked = AddPanel("locked", {0.0f, 0.0f, -1.0f}, events);
    const auto free = AddPanel("free", {3.0f, 0.0f, -1.0f}, events);
    overlays_->ById(locked)->state.recenter = false;
    overlays_->ById(free)->state.saved_transform = Eigen::Affine3f(Eigen::Translation3f(0.1f, 0.0f, -1.0f));

    overlays_->Recenter(*app_);
    EXPECT_TRUE(overlays_->ById(locked)->state.transform.translation().isApprox(Eigen::Vector3f(0.0f, 0.0f, -1.0f), 1e-6f));
    EXPECT_FALSE(Bounds(free).Overlaps(Bounds(locked), 0.0f));
    EXPECT_NEAR(overlays_->ById(free)->state.transform.translation().norm(), Eigen::Vector3f(0.1f, 0.0f, -1.0f).norm(), 1e-4f);
}

TEST_F(Interaction, FootprintDescribesTheEyeRelativeToTheScreen) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.5f, 0.0f, -2.0f}, events);
    const Eigen::Vector2f density(800.0f, 800.0f);

    const auto footprint = OverlayLayout::MeasureFootprint(*overlays_->ById(id), app_->input.hmd, density);
    ASSERT_TRUE(footprint.has_value());
    EXPECT_TRUE(footprint->eye.isApprox(Eigen::Vector3f(-0.5f, 0.0f, 2.0f), 1e-5f));
    EXPECT_TRUE(footprint->size.isApprox(Eigen::Vector2f(1.0f, 0.5f), 1e-5f));
    EXPECT_TRUE(footprint->display_pixels_per_radian.isApprox(density));
    EXPECT_FALSE(OverlayLayout::MeasureFootprint(*overlays_->ById(id), app_->input.hmd, Eigen::Vector2f::Zero()).has_value());
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
