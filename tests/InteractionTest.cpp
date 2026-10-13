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

#include "Config.hpp"
#include "Interaction.hpp"
#include "Keyboard.hpp"
#include "KeyboardPicker.hpp"
#include "ActionBinding.hpp"
#include "Compose.hpp"
#include "Form.hpp"
#include "Container.hpp"
#include "RecordingView.hpp"
#include "RecordingHid.hpp"
#include "ScopedRecordingHid.hpp"
#include "Desktop.hpp"
#include "ScreenInteraction.hpp"
#include "AppState.hpp"
#include "TaskQueue.hpp"
#include "Watch.hpp"
#include "Yaml.hpp"

namespace {

constexpr VkExtent2D kPanelExtent{200, 100};
constexpr float kFrameSeconds = 1.0f / 90.0f;
constexpr float kSlideRate = 4.0f;

class LaserInteraction : public testing::Test {
  protected:
    auto SetUp() -> void override {
        *g_interaction = Interaction();
        g_config->transform_values.clear();
        g_config->curve_values.clear();
        g_config->allow_sliding = true;
        g_config->grab_smoothing = false;
        g_config->pointer_smoothing = false;
        g_interaction->SetHmd(Eigen::Affine3f::Identity());
        m_overlays = Container::CreateEmpty();
        AimAway(1);
    }

    auto AddPanel(
        std::string name,
        const Eigen::Vector3f& position,
        std::shared_ptr<std::vector<RecordedEvent>> events,
        bool desktop = false
    ) -> OverlayId {
        std::unique_ptr<View> state;
        if (desktop) {
            state = std::make_unique<Desktop>(
                name,
                Eigen::Vector2i(kPanelExtent.width, kPanelExtent.height),
                FrameTransform::Normal,
                Eigen::Vector2i::Zero(),
                nullptr,
                ScreenInteraction(Eigen::Vector2f::Zero(), Eigen::Vector2f::Ones(), FrameTransform::Normal)
            );
        } else {
            state = std::make_unique<RecordingView>(kPanelExtent, std::move(events));
        }
        state->SetName(std::move(name));
        state->SetWantVisible(true);
        state->SetInteractable(true);
        state->SetGrabbable(true);
        state->SetRecenter(true);
        state->SetShowHide(true);
        state->SetTransform(Eigen::Translation3f(position) * Eigen::Affine3f::Identity());
        state->SetInteractionTransform(View::UiTransform(kPanelExtent));
        const auto id = state->Id();
        m_overlays->Add(std::move(state));
        return id;
    }

    auto Aim(size_t pointer, const Eigen::Vector3f& origin) -> void {
        g_interaction->Pointers()[pointer].SetPose(Eigen::Translation3f(origin) * Eigen::Affine3f::Identity());
    }

    auto AimAway(size_t pointer) -> void {
        g_interaction->Pointers()[pointer].SetPose(
            Eigen::Affine3f(Eigen::AngleAxisf(std::numbers::pi_v<float>, Eigen::Vector3f::UnitY()))
        );
    }

    auto Frame(const PointerState& left, const PointerState& right = {}) -> std::array<InteractionResult, 2> {
        g_interaction->PreUpdate();
        g_interaction->SetDeltaSeconds(kFrameSeconds);
        g_interaction->Pointers()[0].SetNow(left);
        g_interaction->Pointers()[1].SetNow(right);
        g_interaction->PostUpdate();
        return Interaction::Interact();
    }

    auto Pointer(size_t index) -> ::LaserPointer& { return g_interaction->Pointers()[index]; }

    auto RunTasks() -> void {
        for (int round = 0; round < 4; ++round) {
            std::deque<Task> due;
            g_tasks->TakeDue(due);
            for (auto& task : due) {
                std::visit(
                    [&](auto& value) -> void {
                        using TaskType = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<TaskType, ModifyOverlayTask>) {
                            if (auto* overlay = m_overlays->BySelector(value.selector)) {
                                value.modify(*overlay);
                            }
                        } else if constexpr (std::is_same_v<TaskType, DropOverlayTask>) {
                            m_overlays->Remove(value.selector);
                        } else if constexpr (std::is_same_v<TaskType, CreateOverlayTask>) {
                            if (m_overlays->BySelector(value.selector) == nullptr) {
                                if (auto made = value.create()) {
                                    m_overlays->Add(std::move(made));
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
        const auto corners = Container::Corners(*m_overlays->ById(id));
        EXPECT_TRUE(corners.has_value());
        return corners ? Container::ViewBounds(*corners, g_interaction->Hmd()) : ViewRect{};
    }

    std::unique_ptr<Container> m_overlays;
};

auto Pose(const Eigen::Vector3f& position, const Eigen::Quaternionf& rotation) -> Eigen::Affine3f {
    Eigen::Affine3f pose = Eigen::Affine3f::Identity();
    pose.linear() = rotation.toRotationMatrix();
    pose.translation() = position;
    return pose;
}

auto Combine(const Eigen::Affine3f& first, const Eigen::Affine3f& second) -> Eigen::Affine3f {
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

TEST_F(LaserInteraction, HoverReportsUvThroughInteractionTransform) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);

    Aim(0, Eigen::Vector3f::Zero());
    const auto center = Frame({});
    EXPECT_FLOAT_EQ(center[0].length, 2.0f);
    EXPECT_EQ(Pointer(0).Interaction().hovered_id, id);

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

TEST_F(LaserInteraction, HitsOutsideUvRangeAreIgnored) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);

    Aim(0, {0.6f, 0.0f, 0.0f});
    const auto result = Frame({});
    EXPECT_EQ(result[0].length, 0.0f);
    EXPECT_FALSE(Pointer(0).Interaction().hovered_id.has_value());
    EXPECT_TRUE(events->empty());
}

TEST_F(LaserInteraction, ClickPressAndReleaseAreDelivered) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());

    Frame({});
    Frame({.click = true});
    EXPECT_EQ(Pointer(0).Interaction().clicked_id, id);
    Frame({});
    EXPECT_FALSE(Pointer(0).Interaction().clicked_id.has_value());

    const auto clicks = Events(events, RecordedKind::Press);
    ASSERT_EQ(clicks.size(), 2U);
    EXPECT_TRUE(clicks[0].pressed);
    EXPECT_FALSE(clicks[1].pressed);
    EXPECT_EQ(clicks[0].mode, PointerMode::Left);
}

TEST_F(LaserInteraction, ModifiersSelectPointerMode) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());

    Frame({.click = true, .click_modifier_right = true});
    Frame({.click_modifier_right = true});
    Frame({.click = true, .click_modifier_middle = true});

    const auto clicks = Events(events, RecordedKind::Press);
    ASSERT_EQ(clicks.size(), 3U);
    EXPECT_EQ(clicks[0].mode, PointerMode::Right);
    EXPECT_EQ(clicks[2].mode, PointerMode::Middle);
}

TEST_F(LaserInteraction, ReleaseGoesToTheOverlayThatWasPressed) {
    auto first = std::make_shared<std::vector<RecordedEvent>>();
    auto second = std::make_shared<std::vector<RecordedEvent>>();
    AddPanel("first", {0.0f, 0.0f, -2.0f}, first);
    AddPanel("second", {3.0f, 0.0f, -2.0f}, second);

    Aim(0, Eigen::Vector3f::Zero());
    Frame({.click = true});
    Aim(0, {3.0f, 0.0f, 0.0f});
    Frame({});

    const auto first_clicks = Events(first, RecordedKind::Press);
    ASSERT_EQ(first_clicks.size(), 2U);
    EXPECT_FALSE(first_clicks[1].pressed);
    EXPECT_TRUE(Events(second, RecordedKind::Press).empty());
    EXPECT_EQ(Events(first, RecordedKind::Left).size(), 1U);
}

TEST_F(LaserInteraction, ReleaseIntoEmptySpaceStillReleases) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);

    Aim(0, Eigen::Vector3f::Zero());
    Frame({.click = true});
    AimAway(0);
    const auto result = Frame({});

    EXPECT_EQ(result[0].length, 0.0f);
    EXPECT_EQ(Events(events, RecordedKind::Left).size(), 1U);
    const auto clicks = Events(events, RecordedKind::Press);
    ASSERT_EQ(clicks.size(), 2U);
    EXPECT_FALSE(clicks[1].pressed);
}

TEST_F(LaserInteraction, ReleasingTheTriggerWhileGrabbingReleasesTheClick) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());

    Frame({.click = true});
    Frame({.click = true, .grab = true});
    ASSERT_TRUE(Pointer(0).Interaction().grabbed.has_value());
    Frame({.grab = true});
    EXPECT_FALSE(Pointer(0).Interaction().clicked_id.has_value());
    Frame({});

    const auto clicks = Events(events, RecordedKind::Press);
    ASSERT_EQ(clicks.size(), 2U);
    EXPECT_TRUE(clicks[0].pressed);
    EXPECT_FALSE(clicks[1].pressed);
}

TEST_F(LaserInteraction, ScreenReleasesTheButtonItPressed) {
    ScopedRecordingHid hid;
    ScreenInteraction screen(Eigen::Vector2f::Zero(), Eigen::Vector2f(100.0f, 100.0f), FrameTransform::Normal);

    screen.Press({.pointer = 0, .mode = PointerMode::Right}, true);
    screen.Press({.pointer = 0, .mode = PointerMode::Left}, false);

    const std::vector<ButtonEvent> expected{{.button = kMouseRight, .down = true}, {.button = kMouseRight, .down = false}};
    EXPECT_EQ(hid.Buttons(), expected);
}

TEST_F(LaserInteraction, ScrollIsForwarded) {
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

TEST_F(LaserInteraction, LeftPointerFirstKeepsPrimaryForItself) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());
    Aim(1, {0.1f, 0.0f, 0.0f});
    Pointer(0).SetLastClick(std::chrono::steady_clock::now());
    Pointer(1).SetLastClick(Pointer(0).LastClick() - std::chrono::seconds(1));

    Frame({});

    const auto hovers = Events(events, RecordedKind::Hover);
    ASSERT_EQ(hovers.size(), 2U);
    EXPECT_EQ(hovers[0].pointer, 0U);
    EXPECT_TRUE(hovers[0].primary);
    EXPECT_EQ(hovers[1].pointer, 1U);
    EXPECT_FALSE(hovers[1].primary);
}

TEST_F(LaserInteraction, RecentlyClickedRightPointerGoesFirstAndLowerIndexTakesPrimary) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());
    Aim(1, {0.1f, 0.0f, 0.0f});
    Pointer(1).SetLastClick(std::chrono::steady_clock::now());
    Pointer(0).SetLastClick(Pointer(1).LastClick() - std::chrono::seconds(1));

    Frame({});

    const auto hovers = Events(events, RecordedKind::Hover);
    ASSERT_EQ(hovers.size(), 2U);
    EXPECT_EQ(hovers[0].pointer, 1U);
    EXPECT_TRUE(hovers[0].primary);
    EXPECT_EQ(hovers[1].pointer, 0U);
    EXPECT_TRUE(hovers[1].primary);
    EXPECT_EQ(m_overlays->ById(id)->PrimaryPointer(), 0U);
}

TEST_F(LaserInteraction, GrabFollowsControllerRotationFreely) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());

    const auto grab = Frame({.grab = true});
    ASSERT_TRUE(Pointer(0).Interaction().grabbed.has_value());
    EXPECT_EQ(Pointer(0).Interaction().grabbed->grabbed_id, id);
    ASSERT_TRUE(grab[0].haptics.has_value());
    EXPECT_FLOAT_EQ(grab[0].haptics->intensity, 0.25f);
    EXPECT_TRUE(Events(events, RecordedKind::Hover).empty());

    const Eigen::Affine3f turned = Pose(
        Eigen::Vector3f(0.5f, 0.2f, 0.0f),
        Eigen::Quaternionf(Eigen::AngleAxisf(0.7f, Eigen::Vector3f::UnitY()))
            * Eigen::Quaternionf(Eigen::AngleAxisf(0.3f, Eigen::Vector3f::UnitX()))
    );
    Pointer(0).SetPose(turned);
    const auto held = Frame({.grab = true});
    EXPECT_NEAR(held[0].length, 2.0f, 1e-4f);

    const auto* overlay = m_overlays->ById(id);
    ASSERT_NE(overlay, nullptr);
    const Eigen::Affine3f expected = Combine(turned, Eigen::Affine3f(Eigen::Translation3f(0.0f, 0.0f, -2.0f)));
    EXPECT_TRUE(overlay->Transform().matrix().isApprox(expected.matrix(), 1e-4f));
}

TEST_F(LaserInteraction, DropRemembersPositionRelativeToHead) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());
    g_interaction->SetHmd(
        Pose(Eigen::Vector3f(0.0f, 1.6f, 0.0f), Eigen::Quaternionf(Eigen::AngleAxisf(0.4f, Eigen::Vector3f::UnitY())))
    );

    Frame({.grab = true});
    Aim(0, {0.3f, 0.0f, 0.0f});
    Frame({.grab = true});
    Frame({});

    const auto* overlay = m_overlays->ById(id);
    ASSERT_TRUE(overlay->SavedTransform().has_value());
    const Eigen::Affine3f expected = Combine(Eigen::Affine3f(g_interaction->Hmd().matrix().inverse()), overlay->Transform());
    EXPECT_TRUE(overlay->SavedTransform()->matrix().isApprox(expected.matrix(), 1e-4f));
    const auto saved = std::ranges::find(
        g_config->transform_values,
        std::string("panel"),
        &std::pair<std::string, Eigen::Affine3f>::first
    );
    ASSERT_NE(saved, g_config->transform_values.end());
    EXPECT_TRUE(saved->second.matrix().isApprox(expected.matrix(), 1e-4f));
}

TEST_F(LaserInteraction, StickPushesAwayAndScalesWithDistance) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());

    const float step = std::exp(kSlideRate * kFrameSeconds);
    Frame({.grab = true});
    Frame({.scroll_y = 1.0f, .grab = true});
    const auto* overlay = m_overlays->ById(id);
    const Eigen::Matrix3f pushed = overlay->Transform().linear();
    EXPECT_NEAR(overlay->Transform().translation().z(), -2.0f * step, 1e-5f);
    EXPECT_NEAR(pushed.col(0).norm(), step, 1e-5f);

    Frame({.scroll_y = -1.0f, .grab = true});
    Frame({.scroll_y = -1.0f, .click = true, .grab = true});
    const Eigen::Matrix3f pulled = overlay->Transform().linear();
    EXPECT_NEAR(overlay->Transform().translation().z(), -2.0f / step, 1e-5f);
    EXPECT_NEAR(pulled.col(0).norm(), 1.0f / step, 1e-5f);
    EXPECT_NE(Pointer(0).Interaction().mode, PointerMode::Special);
}

TEST_F(LaserInteraction, StickScalesWithDistanceFromTheHead) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, {0.0f, 0.0f, -0.5f});

    const float pushed_to = std::fma(1.5f, std::exp(kSlideRate * kFrameSeconds), 0.5f);
    Frame({.grab = true});
    Frame({.scroll_y = 1.0f, .grab = true});
    const auto* overlay = m_overlays->ById(id);
    const Eigen::Matrix3f pushed = overlay->Transform().linear();
    EXPECT_NEAR(overlay->Transform().translation().z(), -pushed_to, 1e-5f);
    EXPECT_NEAR(pushed.col(0).norm(), pushed_to / 2.0f, 1e-5f);
}

TEST_F(LaserInteraction, StickSlidesFarOverlaysFasterThanNearOnes) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto near = AddPanel("near", {0.0f, 0.0f, -1.0f}, events);
    const auto far = AddPanel("far", {3.0f, 0.0f, -4.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());
    Frame({.grab = true});
    for (int frame = 0; frame < 45; ++frame) {
        Frame({.scroll_y = 1.0f, .grab = true});
    }
    Frame({});
    const float near_moved = m_overlays->ById(near)->Transform().translation().norm() - 1.0f;
    EXPECT_NEAR(near_moved, std::exp(kSlideRate * 0.5f) - 1.0f, 0.01f);

    Aim(0, Eigen::Vector3f::Zero());
    Pointer(0).Pose().linear() = Eigen::Matrix3f(Eigen::AngleAxisf(-std::atan2(3.0f, 4.0f), Eigen::Vector3f::UnitY()));
    Frame({.grab = true});
    ASSERT_TRUE(Pointer(0).Interaction().grabbed.has_value());
    EXPECT_EQ(Pointer(0).Interaction().grabbed->grabbed_id, far);
    for (int frame = 0; frame < 9; ++frame) {
        Frame({.scroll_y = 1.0f, .grab = true});
    }
    const float far_moved = m_overlays->ById(far)->Transform().translation().norm() - 5.0f;
    const float near_first_tenth = std::exp(kSlideRate * 0.1f) - 1.0f;
    EXPECT_NEAR(far_moved, 5.0f * near_first_tenth, 0.01f);
}

TEST_F(LaserInteraction, StickDriftDoesNotMoveOrResize) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());

    Frame({.grab = true});
    for (int frame = 0; frame < 100; ++frame) {
        Frame({.scroll_y = frame % 2 == 0 ? 0.15f : -0.1f, .grab = true});
    }
    const auto* overlay = m_overlays->ById(id);
    const Eigen::Matrix3f linear = overlay->Transform().linear();
    EXPECT_TRUE(overlay->Transform().translation().isApprox(Eigen::Vector3f(0.0f, 0.0f, -2.0f), 1e-6f));
    EXPECT_NEAR(linear.col(0).norm(), 1.0f, 1e-6f);
}

TEST_F(LaserInteraction, GrabSmoothingRemovesHandTremor) {
    g_config->grab_smoothing = true;
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());
    Frame({.grab = true});

    float wobble = 0.0f;
    for (int frame = 0; frame < 90; ++frame) {
        Aim(0, {frame % 2 == 0 ? 0.002f : -0.002f, 0.0f, 0.0f});
        Frame({.grab = true});
        if (frame > 30) {
            wobble = std::max(wobble, std::abs(m_overlays->ById(id)->Transform().translation().x()));
        }
    }
    EXPECT_LT(wobble, 0.0005f);
}

TEST_F(LaserInteraction, GrabSmoothingKeepsUpWithDeliberateMoves) {
    g_config->grab_smoothing = true;
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());
    Frame({.grab = true});

    for (int frame = 1; frame <= 30; ++frame) {
        Aim(0, {0.5f * static_cast<float>(frame) / 30.0f, 0.0f, 0.0f});
        Frame({.grab = true});
    }
    const float lag = 0.5f - m_overlays->ById(id)->Transform().translation().x();
    EXPECT_LT(lag, 0.05f);
    for (int frame = 0; frame < 30; ++frame) {
        Frame({.grab = true});
    }
    EXPECT_NEAR(m_overlays->ById(id)->Transform().translation().x(), 0.5f, 0.001f);
}

TEST_F(LaserInteraction, ShowPlacesOverlaysRelativeToCurrentHead) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -1.0f}, events);
    auto* overlay = m_overlays->ById(id);
    overlay->SetWantVisible(false);
    overlay->SetSavedTransform(Eigen::Affine3f(Eigen::Translation3f(0.2f, -0.1f, -1.5f)));

    g_interaction->SetHmd(Eigen::Translation3f(0.0f, 1.6f, 0.0f) * Eigen::Affine3f::Identity());
    m_overlays->ShowHide();
    ASSERT_TRUE(overlay->WantVisible());
    EXPECT_TRUE(overlay->Transform().translation().isApprox(Eigen::Vector3f(0.2f, 1.5f, -1.5f), 1e-5f));

    m_overlays->ShowHide();
    EXPECT_FALSE(overlay->WantVisible());

    const Eigen::Affine3f turned = Pose(
        Eigen::Vector3f(1.0f, 1.6f, 2.0f),
        Eigen::Quaternionf(Eigen::AngleAxisf(std::numbers::pi_v<float> / 2.0f, Eigen::Vector3f::UnitY()))
    );
    g_interaction->SetHmd(turned);
    m_overlays->ShowHide();
    const Eigen::Affine3f expected = Combine(turned, *overlay->SavedTransform());
    EXPECT_TRUE(overlay->Transform().matrix().isApprox(expected.matrix(), 1e-5f));
    const Eigen::Vector3f to_overlay = overlay->Transform().translation() - turned.translation();
    EXPECT_NEAR(to_overlay.norm(), Eigen::Vector3f(0.2f, -0.1f, -1.5f).norm(), 1e-5f);
    const Eigen::Matrix3f overlay_linear = overlay->Transform().linear();
    const Eigen::Matrix3f head_linear = turned.linear();
    EXPECT_TRUE(overlay_linear.col(2).isApprox(head_linear.col(2), 1e-5f));
}

TEST_F(LaserInteraction, ShowWhileLyingDownPlacesOverlayWhereYouLook) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -1.0f}, events);
    auto* overlay = m_overlays->ById(id);
    overlay->SetWantVisible(false);
    overlay->SetSavedTransform(Eigen::Affine3f(Eigen::Translation3f(0.0f, 0.0f, -1.0f)));

    g_interaction->SetHmd(Pose(
        Eigen::Vector3f(0.0f, 0.5f, 0.0f),
        Eigen::Quaternionf(Eigen::AngleAxisf(std::numbers::pi_v<float> / 2.0f, Eigen::Vector3f::UnitX()))
    ));
    m_overlays->ShowHide();
    EXPECT_TRUE(overlay->Transform().translation().isApprox(Eigen::Vector3f(0.0f, 1.5f, 0.0f), 1e-5f));
}

TEST_F(LaserInteraction, RecenterMovesOnlyVisibleOverlays) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto shown = AddPanel("shown", {0.0f, 0.0f, -1.0f}, events);
    const auto hidden = AddPanel("hidden", {5.0f, 0.0f, -1.0f}, events);
    m_overlays->ById(hidden)->SetWantVisible(false);
    for (const auto id : {shown, hidden}) {
        m_overlays->ById(id)->SetSavedTransform(Eigen::Affine3f(Eigen::Translation3f(0.0f, 0.0f, -1.0f)));
    }

    g_interaction->SetHmd(Eigen::Translation3f(3.0f, 1.0f, 0.0f) * Eigen::Affine3f::Identity());
    m_overlays->Recenter();
    EXPECT_TRUE(m_overlays->ById(shown)->Transform().translation().isApprox(Eigen::Vector3f(3.0f, 1.0f, -1.0f), 1e-5f));
    EXPECT_TRUE(m_overlays->ById(hidden)->Transform().translation().isApprox(Eigen::Vector3f(5.0f, 0.0f, -1.0f), 1e-5f));
}

TEST_F(LaserInteraction, DefaultLayoutArcsScreensAroundTheHead) {
    std::vector<std::unique_ptr<Desktop>> screens;
    for (const int32_t x : {1920, 0, 3840}) {
        screens.push_back(
            std::make_unique<Desktop>(
                std::format("screen{}", screens.size()),
                Eigen::Vector2i(1920, 1080),
                FrameTransform::Normal,
                Eigen::Vector2i(x, 0),
                nullptr,
                ScreenInteraction(Eigen::Vector2f::Zero(), Eigen::Vector2f::Ones(), FrameTransform::Normal)
            )
        );
        screens.back()->SetSpawnScale(1.5f);
    }
    Desktop::ArrangeInFrontOfHead(screens);

    const auto& middle = *screens[0];
    EXPECT_NEAR(middle.SpawnPoint().x(), 0.0f, 1e-5f);
    EXPECT_LT(middle.SpawnPoint().y(), -0.1f);
    EXPECT_LT(middle.SpawnPoint().z(), -0.9f);
    EXPECT_LT(screens[1]->SpawnPoint().x(), -0.5f);
    EXPECT_GT(screens[2]->SpawnPoint().x(), 0.5f);
    for (const auto& screen : screens) {
        EXPECT_NEAR(screen->SpawnPoint().norm(), 1.0f, 1e-5f);
        const Eigen::Vector3f facing = screen->SpawnRotation() * Eigen::Vector3f::UnitZ();
        EXPECT_TRUE(facing.isApprox(-screen->SpawnPoint().normalized(), 1e-4f)) << screen->Name();
    }
}

TEST_F(LaserInteraction, KeyboardSpawnsBelowViewFacingTheHead) {
    auto keyboard = Keyboard::Create(nullptr);
    ASSERT_TRUE(keyboard.has_value());
    const auto& state = **keyboard;
    EXPECT_FLOAT_EQ(state.SpawnPoint().x(), 0.0f);
    EXPECT_LT(state.SpawnPoint().y(), -0.3f);
    EXPECT_LT(state.SpawnPoint().z(), 0.0f);
    const Eigen::Vector3f facing = state.SpawnRotation() * Eigen::Vector3f::UnitZ();
    EXPECT_TRUE(facing.isApprox(-state.SpawnPoint().normalized(), 1e-4f));
}

TEST_F(LaserInteraction, KeyboardUsesTheConfiguredProfile) {
    g_config->keyboard_profile = "full-size";
    auto keyboard = Keyboard::Create(nullptr);
    g_config->keyboard_profile = "keychron-v1-ansi";
    ASSERT_TRUE(keyboard.has_value());
    const auto meta = (*keyboard)->Composer()->Meta();
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->extent.width, (23U * 80U) + 28U);

    auto compact = Keyboard::Create(nullptr);
    ASSERT_TRUE(compact.has_value());
    EXPECT_NEAR((*compact)->SpawnScale() * 23.35f, (*keyboard)->SpawnScale() * 16.35f, 1e-4f);
}

TEST_F(LaserInteraction, LayoutKeyOpensAPickerThatSwitchesKeyboards) {
    auto created = Keyboard::Create(nullptr);
    ASSERT_TRUE(created.has_value());
    (*created)->SetWantVisible(true);
    m_overlays->Add(std::move(*created));
    auto* keyboard = m_overlays->ByName(kKeyboardName);
    ASSERT_TRUE(keyboard->Composer()->ShouldRender().has_value());
    auto* legend = keyboard->Composer()->FindText("Layout");
    ASSERT_NE(legend, nullptr);
    ASSERT_NE(legend->Parent(), nullptr);

    legend->Parent()->OnPress(PointerMode::Left);
    RunTasks();
    auto* picker = m_overlays->ByName(kKeyboardPickerName);
    ASSERT_NE(picker, nullptr);
    EXPECT_EQ(g_config->keyboard_profile, "keychron-v1-ansi");

    auto* choice = picker->Composer()->Root()->Find("jis-tkl");
    ASSERT_NE(choice, nullptr);
    choice->OnRelease();
    RunTasks();
    EXPECT_EQ(g_config->keyboard_profile, "jis-tkl");
    EXPECT_EQ(m_overlays->ByName(kKeyboardPickerName), nullptr);
    g_config->keyboard_profile = "keychron-v1-ansi";
}

TEST_F(LaserInteraction, ProfileKeySwapsTheKeyboardInPlace) {
    auto created = Keyboard::Create(nullptr);
    ASSERT_TRUE(created.has_value());
    auto& original = **created;
    original.SetWantVisible(true);
    original.SetShowHide(true);
    original.SetTransform(Pose(Eigen::Vector3f(0.2f, -0.4f, -0.9f), Eigen::Quaternionf::Identity()));
    original.Transform().linear() *= original.SpawnScale();
    original.SetSavedTransform(original.Transform());
    const auto before_id = original.Id();
    const Eigen::Affine3f before = original.Transform();
    m_overlays->Add(std::move(*created));
    g_config->transform_values = {{std::string(kKeyboardName), before}};

    Keyboard::SwitchProfile(nullptr, "full-size");
    EXPECT_EQ(g_config->keyboard_profile, "full-size");
    RunTasks();

    auto* swapped = m_overlays->ByName(kKeyboardName);
    ASSERT_NE(swapped, nullptr);
    EXPECT_EQ(swapped->Id(), before_id);
    EXPECT_TRUE(swapped->WantVisible());
    EXPECT_TRUE(swapped->ShowHide());
    EXPECT_EQ(swapped->Composer()->Meta()->extent.width, (23U * 80U) + 28U);
    const float factor = 23.35f / 16.35f;
    const Eigen::Matrix3f linear = swapped->Transform().linear();
    const Eigen::Matrix3f linear_before = before.linear();
    EXPECT_TRUE(swapped->Transform().translation().isApprox(before.translation(), 1e-6f));
    EXPECT_NEAR(linear.col(0).norm(), linear_before.col(0).norm() * factor, 1e-5f);
    ASSERT_TRUE(swapped->SavedTransform().has_value());
    EXPECT_TRUE(g_config->transform_values.front().second.matrix().isApprox(swapped->SavedTransform()->matrix()));
    g_config->keyboard_profile = "keychron-v1-ansi";
}

TEST_F(LaserInteraction, DroppingOntoAnotherOverlayKeepsTheOverlap) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto fixed = AddPanel("fixed", {0.0f, 0.0f, -2.0f}, events);
    const auto moving = AddPanel("moving", {0.3f, 0.0f, -2.0f}, events);

    Aim(0, {0.6f, 0.0f, 0.0f});
    Frame({.grab = true});
    ASSERT_TRUE(Pointer(0).Interaction().grabbed.has_value());
    Frame({});

    EXPECT_TRUE(m_overlays->ById(moving)->Transform().translation().isApprox(Eigen::Vector3f(0.3f, 0.0f, -2.0f), 1e-6f));
    EXPECT_TRUE(Container::Overlaps(Bounds(moving), Bounds(fixed), 0.0f));
}

TEST_F(LaserInteraction, ShowHideRestoresOnlyWhatWasOpen) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto open = AddPanel("open", {0.0f, 0.0f, -1.0f}, events);
    const auto closed = AddPanel("closed", {2.0f, 0.0f, -1.0f}, events);
    m_overlays->ById(closed)->SetWantVisible(false);

    m_overlays->ShowHide();
    EXPECT_FALSE(m_overlays->ById(open)->WantVisible());
    EXPECT_FALSE(m_overlays->ById(closed)->WantVisible());

    m_overlays->ShowHide();
    EXPECT_TRUE(m_overlays->ById(open)->WantVisible());
    EXPECT_FALSE(m_overlays->ById(closed)->WantVisible());
}

TEST_F(LaserInteraction, ShowHideIncludesOverlaysOpenedOutsideTheSet) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto screen = AddPanel("screen", {0.0f, 0.0f, -1.0f}, events);
    const auto extra = AddPanel("extra", {2.0f, 0.0f, -1.0f}, events);
    m_overlays->ById(extra)->SetShowHide(false);

    m_overlays->ShowHide();
    EXPECT_FALSE(m_overlays->ById(screen)->WantVisible());
    EXPECT_FALSE(m_overlays->ById(extra)->WantVisible());

    m_overlays->ShowHide();
    EXPECT_TRUE(m_overlays->ById(screen)->WantVisible());
    EXPECT_TRUE(m_overlays->ById(extra)->WantVisible());
}

TEST_F(LaserInteraction, CurvedScreensCoverTheirRealViewAngle) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto screen = AddPanel("screen", {0.0f, 0.0f, -1.0f}, events, true);
    auto* overlay = m_overlays->ById(screen);
    const auto head = Eigen::Affine3f::Identity();
    const auto flat = Container::ViewBounds(*Container::Corners(*overlay), head);
    overlay->SetCurvature(0.25f);
    const auto curved = Container::ViewBounds(*Container::Corners(*overlay), head);
    EXPECT_GT(curved.max_azimuth - curved.min_azimuth, flat.max_azimuth - flat.min_azimuth);
    EXPECT_GT(curved.max_elevation - curved.min_elevation, flat.max_elevation - flat.min_elevation);
    EXPECT_NEAR(
        Container::ArcPerChord(0.25f),
        (std::numbers::pi_v<float> / 4.0f) / std::sin(std::numbers::pi_v<float> / 4.0f),
        1e-5f
    );
}

TEST_F(LaserInteraction, ShowHideKeepsOverlapsTheUserMade) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto screen = AddPanel("screen", {0.0f, 0.0f, -1.0f}, events, true);
    const auto keyboard = AddPanel(std::string(kKeyboardName), {0.0f, -0.3f, -1.0f}, events);
    m_overlays->ById(screen)->SetSavedTransform(m_overlays->ById(screen)->Transform());
    ASSERT_TRUE(Container::Overlaps(Bounds(keyboard), Bounds(screen), 0.0f));

    Aim(0, {0.0f, -0.45f, 0.0f});
    Frame({.grab = true});
    ASSERT_EQ(Pointer(0).Interaction().grabbed->grabbed_id, keyboard);
    Frame({});

    m_overlays->ShowHide();
    g_interaction->SetHmd(Eigen::Translation3f(0.0f, 1.6f, 0.0f) * Eigen::Affine3f::Identity());
    m_overlays->ShowHide();
    ASSERT_TRUE(m_overlays->ById(keyboard)->WantVisible());
    EXPECT_TRUE(m_overlays->ById(keyboard)->Transform().translation().isApprox(Eigen::Vector3f(0.0f, 1.3f, -1.0f), 1e-5f));
    EXPECT_TRUE(Container::Overlaps(Bounds(keyboard), Bounds(screen), 0.0f));
}

TEST_F(LaserInteraction, ShowHideClearsOverlapsTheUserDidNotMake) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto screen = AddPanel("screen", {0.0f, 0.0f, -1.0f}, events, true);
    const auto keyboard = AddPanel(std::string(kKeyboardName), {0.0f, -0.3f, -1.0f}, events);
    for (const auto id : {screen, keyboard}) {
        auto& state = *m_overlays->ById(id);
        state.SetSavedTransform(state.Transform());
    }
    ASSERT_TRUE(Container::Overlaps(Bounds(keyboard), Bounds(screen), 0.0f));

    m_overlays->ShowHide();
    m_overlays->ShowHide();
    ASSERT_TRUE(m_overlays->ById(keyboard)->WantVisible());
    EXPECT_TRUE(m_overlays->ById(screen)->Transform().translation().isApprox(Eigen::Vector3f(0.0f, 0.0f, -1.0f), 1e-6f));
    EXPECT_FALSE(Container::Overlaps(Bounds(keyboard), Bounds(screen), 0.0f));
}

TEST_F(LaserInteraction, DraggingAwayForgetsTheOverlap) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto screen = AddPanel("screen", {0.0f, 0.0f, -1.0f}, events, true);
    const auto keyboard = AddPanel(std::string(kKeyboardName), {0.0f, -0.3f, -1.0f}, events);
    m_overlays->ById(screen)->SetSavedTransform(m_overlays->ById(screen)->Transform());

    Aim(0, {0.0f, -0.45f, 0.0f});
    Frame({.grab = true});
    Frame({});
    Frame({.grab = true});
    Aim(0, {0.0f, -0.95f, 0.0f});
    Frame({.grab = true});
    Frame({});
    ASSERT_FALSE(Container::Overlaps(Bounds(keyboard), Bounds(screen), 0.0f));

    m_overlays->ById(keyboard)->SetSavedTransform(Eigen::Affine3f(Eigen::Translation3f(0.0f, -0.3f, -1.0f)));
    m_overlays->Recenter();
    EXPECT_FALSE(Container::Overlaps(Bounds(keyboard), Bounds(screen), 0.0f));
}

TEST_F(LaserInteraction, RecenteringCurvedScreensKeepsTheirLayoutWhileLyingDown) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    std::vector<OverlayId> screens;
    for (const float x : {-0.52f, 0.52f}) {
        const auto id = AddPanel(std::format("screen{}", screens.size()), {x, 0.0f, -1.0f}, events, true);
        auto* screen = m_overlays->ById(id);
        screen->SetSavedTransform(screen->Transform());
        screen->SetCurvature(0.3f);
        screens.push_back(id);
    }
    const Eigen::Affine3f lying(Eigen::AngleAxisf(std::numbers::pi_v<float> / 2.0f, Eigen::Vector3f::UnitZ()));
    g_interaction->SetHmd(lying);
    m_overlays->Recenter();
    for (const auto id : screens) {
        const auto* screen = m_overlays->ById(id);
        const Eigen::Affine3f expected = lying * *screen->SavedTransform();
        EXPECT_TRUE(screen->Transform().matrix().isApprox(expected.matrix(), 1e-5f));
    }
    g_interaction->SetHmd(Eigen::Affine3f::Identity());
}

TEST_F(LaserInteraction, RecenterClearsOverlapsThatWereNotThereBefore) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto screen = AddPanel("screen", {0.0f, 0.0f, -1.0f}, events, true);
    const auto keyboard = AddPanel(std::string(kKeyboardName), {0.0f, -1.0f, -1.0f}, events);
    m_overlays->ById(screen)->SetSavedTransform(Eigen::Affine3f(Eigen::Translation3f(0.0f, 0.0f, -1.0f)));
    m_overlays->ById(keyboard)->SetSavedTransform(Eigen::Affine3f(Eigen::Translation3f(0.0f, -0.3f, -1.0f)));
    ASSERT_FALSE(Container::Overlaps(Bounds(keyboard), Bounds(screen), 0.0f));

    m_overlays->Recenter();
    EXPECT_TRUE(m_overlays->ById(screen)->Transform().translation().isApprox(Eigen::Vector3f(0.0f, 0.0f, -1.0f), 1e-6f));
    EXPECT_FALSE(Container::Overlaps(Bounds(keyboard), Bounds(screen), 0.0f));
    EXPECT_NEAR(m_overlays->ById(keyboard)->Transform().translation().norm(), Eigen::Vector3f(0.0f, -0.3f, -1.0f).norm(), 1e-4f);
}

TEST_F(LaserInteraction, ShowKeepsKeyboardClearOfScreen) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto screen = AddPanel("screen", {0.0f, 0.0f, -1.0f}, events, true);
    const auto keyboard = AddPanel(std::string(kKeyboardName), {0.0f, 0.0f, -1.0f}, events);
    const Eigen::Affine3f screen_spot(Eigen::Translation3f(0.0f, -0.1f, -1.0f));
    for (const auto id : {screen, keyboard}) {
        m_overlays->ById(id)->SetWantVisible(false);
    }
    m_overlays->ById(screen)->SetSavedTransform(screen_spot);
    m_overlays->ById(keyboard)->SetSavedTransform(Eigen::Affine3f(Eigen::Translation3f(0.0f, -0.4f, -1.0f)));

    m_overlays->ShowHide();
    ASSERT_TRUE(m_overlays->ById(keyboard)->WantVisible());
    EXPECT_TRUE(m_overlays->ById(screen)->Transform().matrix().isApprox(screen_spot.matrix(), 1e-6f));
    EXPECT_FALSE(Container::Overlaps(Bounds(keyboard), Bounds(screen), 0.0f));
    EXPECT_LT(Bounds(keyboard).max_elevation, Bounds(screen).min_elevation);
}

TEST_F(LaserInteraction, RecenterStepsAroundLockedOverlays) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto locked = AddPanel("locked", {0.0f, 0.0f, -1.0f}, events);
    const auto free = AddPanel("free", {3.0f, 0.0f, -1.0f}, events);
    m_overlays->ById(locked)->SetRecenter(false);
    m_overlays->ById(free)->SetSavedTransform(Eigen::Affine3f(Eigen::Translation3f(0.1f, 0.0f, -1.0f)));

    m_overlays->Recenter();
    EXPECT_TRUE(m_overlays->ById(locked)->Transform().translation().isApprox(Eigen::Vector3f(0.0f, 0.0f, -1.0f), 1e-6f));
    EXPECT_FALSE(Container::Overlaps(Bounds(free), Bounds(locked), 0.0f));
    EXPECT_NEAR(m_overlays->ById(free)->Transform().translation().norm(), Eigen::Vector3f(0.1f, 0.0f, -1.0f).norm(), 1e-4f);
}

TEST_F(LaserInteraction, FootprintDescribesTheEyeRelativeToTheScreen) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.5f, 0.0f, -2.0f}, events);
    const Eigen::Vector2f density(800.0f, 800.0f);

    const auto footprint = Container::MeasureFootprint(*m_overlays->ById(id), g_interaction->Hmd(), density);
    ASSERT_TRUE(footprint.has_value());
    EXPECT_TRUE(footprint->eye.isApprox(Eigen::Vector3f(-0.5f, 0.0f, 2.0f), 1e-5f));
    EXPECT_TRUE(footprint->size.isApprox(Eigen::Vector2f(1.0f, 0.5f), 1e-5f));
    EXPECT_TRUE(footprint->display_pixels_per_radian.isApprox(density));
    EXPECT_FALSE(Container::MeasureFootprint(*m_overlays->ById(id), g_interaction->Hmd(), Eigen::Vector2f::Zero()).has_value());
}

TEST_F(LaserInteraction, GrabbedLaserStaysOnTheGrabbedPoint) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.3f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());
    Frame({.grab = true});
    const auto results = Frame({.grab = true});

    const Eigen::Vector3f grabbed(0.0f, 0.0f, -2.0f);
    ASSERT_TRUE(results[0].target.has_value());
    EXPECT_TRUE(results[0].target->isApprox(grabbed, 1e-5f));
    EXPECT_NEAR(results[0].length, 2.0f, 1e-5f);

    Aim(0, {0.0f, 0.2f, 0.0f});
    const auto moved = Frame({.grab = true});
    const Eigen::Vector3f point(0.0f, 0.2f, -2.0f);
    ASSERT_TRUE(moved[0].target.has_value());
    EXPECT_TRUE(moved[0].target->isApprox(point, 1e-5f));
    const Eigen::Affine3f line = LaserPointer::LinePose(Pointer(0).Pose(), moved[0].target);
    const Eigen::Matrix3f line_linear = line.linear();
    EXPECT_TRUE((line_linear * -Eigen::Vector3f::UnitZ()).isApprox(-Eigen::Vector3f::UnitZ(), 1e-5f));
    EXPECT_TRUE(m_overlays->ById(id)->Transform().translation().isApprox(Eigen::Vector3f(0.3f, 0.2f, -2.0f), 1e-6f));

    const auto released = Frame({});
    EXPECT_FALSE(released[0].target.has_value());
}

TEST_F(LaserInteraction, PointerSmoothingSteadiesTheLaser) {
    g_config->pointer_smoothing = true;
    Aim(0, Eigen::Vector3f::Zero());
    Frame({});
    Frame({});
    float drift = 0.0f;
    for (int i = 0; i < 30; ++i) {
        const float jitter = (i % 2 == 0 ? 1.0f : -1.0f) * 0.004f;
        g_interaction->Pointers()[0].SetPose(Eigen::Affine3f(Eigen::AngleAxisf(jitter, Eigen::Vector3f::UnitY())));
        Frame({});
        const Eigen::Matrix3f linear = Pointer(0).Pose().linear();
        drift = std::max(drift, std::abs((linear * -Eigen::Vector3f::UnitZ()).x()));
    }
    EXPECT_LT(drift, 0.0015f);

    for (int i = 0; i < 30; ++i) {
        g_interaction->Pointers()[0].SetPose(Eigen::Affine3f(Eigen::AngleAxisf(0.5f, Eigen::Vector3f::UnitY())));
        Frame({});
    }
    const Eigen::Matrix3f settled = Pointer(0).Pose().linear();
    EXPECT_NEAR(std::asin(-(settled * -Eigen::Vector3f::UnitZ()).x()), 0.5f, 0.01f);
    g_config->pointer_smoothing = false;
}

TEST_F(LaserInteraction, HandAttachedOverlaysIgnorePointerSmoothing) {
    g_config->pointer_smoothing = true;
    Frame({});
    g_interaction->Pointers()[0].SetPose(Eigen::Affine3f(Eigen::Translation3f(0.3f, 0.1f, -0.2f)));
    Frame({});
    View watch;
    watch.SetAttachedTo(RelativeTo::LeftHand);
    watch.AutoMovement();
    EXPECT_TRUE(watch.Transform().translation().isApprox(Eigen::Vector3f(0.3f, 0.1f, -0.2f) + watch.SpawnPoint(), 1e-5f));
    EXPECT_FALSE(Pointer(0).Pose().translation().isApprox(Eigen::Vector3f(0.3f, 0.1f, -0.2f), 1e-3f));
    g_config->pointer_smoothing = false;
}

TEST_F(LaserInteraction, GrabAndTriggerBendsByPullingTheHand) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    auto* overlay = m_overlays->ById(id);
    Aim(0, {0.0f, 0.0f, -0.4f});
    Frame({.grab = true});
    ASSERT_TRUE(Pointer(0).Interaction().grabbed.has_value());
    const Eigen::Vector3f placed = overlay->Transform().translation();

    Frame({.click = true, .grab = true});
    Aim(0, {0.0f, 0.0f, -0.2f});
    Frame({.click = true, .grab = true});
    ASSERT_TRUE(overlay->Curvature().has_value());
    EXPECT_NEAR(*overlay->Curvature(), 0.3f, 1e-4f);
    EXPECT_TRUE(overlay->Transform().translation().isApprox(placed, 1e-5f));
    EXPECT_TRUE(Events(events, RecordedKind::Press).empty());

    Aim(0, {0.0f, 0.0f, -0.6f});
    Frame({.click = true, .grab = true});
    EXPECT_FALSE(overlay->Curvature().has_value());

    Aim(0, {0.0f, 0.0f, -0.2f});
    Frame({.click = true, .grab = true});
    Frame({.grab = true});
    Frame({});
    EXPECT_TRUE(std::ranges::any_of(g_config->curve_values, [](const auto& entry) -> bool { return entry.first == "panel"; }));
    std::erase_if(g_config->curve_values, [](const auto& entry) -> bool { return entry.first == "panel"; });
}

TEST_F(LaserInteraction, ScrollWithOtherHandWhileGrabbedCurves) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());
    Aim(1, {0.1f, 0.0f, 0.0f});

    Frame({.grab = true});
    Frame({.grab = true}, {.scroll_y = -10.0f});

    auto* overlay = m_overlays->ById(id);
    ASSERT_TRUE(overlay->Curvature().has_value());
    EXPECT_NEAR(*overlay->Curvature(), 0.1f, 1e-5f);
    EXPECT_TRUE(Events(events, RecordedKind::Scroll).empty());

    Frame({.grab = true}, {.scroll_y = 20.0f});
    EXPECT_FALSE(overlay->Curvature().has_value());

    Frame({}, {});
    EXPECT_TRUE(std::ranges::none_of(g_config->curve_values, [](const auto& entry) -> bool { return entry.first == "panel"; }));
}

TEST_F(LaserInteraction, ActionButtonClickRunsItsAction) {
    const auto parsed = Yaml::Parse(R"(
width: 0.3
form:
  width: 200
  height: 100
components:
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
    const auto config = parsed->get<FormDocument>();
    auto canvas = Compose::Create(Form::FromDocument(config, "buttons", ActionBinding::Bind));
    ASSERT_TRUE(canvas.has_value());

    auto state = std::make_unique<View>(std::move(*canvas));
    state->SetName("buttons");
    state->SetWantVisible(true);
    state->SetInteractable(true);
    state->SetTransform(Eigen::Translation3f(0.0f, 0.0f, -1.0f) * Eigen::Affine3f::Identity());
    state->SetInteractionTransform(View::UiTransform(kPanelExtent));
    m_overlays->Add(std::move(state));

    const bool before = g_config->notifications_enabled;
    Aim(0, Eigen::Vector3f::Zero());
    Frame({});
    Frame({.click = true});
    EXPECT_EQ(g_config->notifications_enabled, before);
    Frame({});
    EXPECT_EQ(g_config->notifications_enabled, !before);
    g_config->notifications_enabled = before;
}

TEST(WatchButtons, EveryButtonIsTheClickTargetAtItsCentre) {
    auto overlays = Container::CreateEmpty();
    auto watch = Watch::Create();
    ASSERT_TRUE(watch.has_value());
    auto& compose = *(*watch)->Composer();
    ASSERT_TRUE(compose.ShouldRender().has_value());
    const auto meta = compose.Meta();
    ASSERT_TRUE(meta.has_value());
    const Eigen::Vector2f extent(static_cast<float>(meta->extent.width), static_cast<float>(meta->extent.height));
    for (const auto* text : {"Vol +", "Vol -"}) {
        auto* button = compose.FindText(text);
        ASSERT_NE(button, nullptr) << text;
        const auto& items = compose.Items();
        const auto own = std::ranges::find_if(items, [button](const FormItem& item) -> bool {
            const auto* interact = std::get_if<InteractItem>(&item);
            return interact != nullptr && interact->owner == button;
        });
        ASSERT_NE(own, items.end()) << text;
        const auto& rect = std::get<InteractItem>(*own).rect;
        const Eigen::Vector2f center(rect.x + rect.w / 2.0f, rect.y + rect.h / 2.0f);
        const auto index = compose.InteractiveAt(center.cwiseQuotient(extent));
        ASSERT_TRUE(index.has_value()) << text;
        EXPECT_EQ(std::get<InteractItem>(items.at(*index)).owner, button)
            << text << " is covered by " << std::get<InteractItem>(items.at(*index)).owner->Name();
    }
}
