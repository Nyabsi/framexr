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
#include "Input.hpp"
#include "Keyboard.hpp"
#include "Modular.hpp"
#include "OverlayContainer.hpp"
#include "OverlayLayout.hpp"
#include "RecordingBackend.hpp"
#include "RecordingHid.hpp"
#include "ScopedRecordingHid.hpp"
#include "Screen.hpp"
#include "ScreenInteraction.hpp"
#include "State.hpp"
#include "Task.hpp"
#include "Yaml.hpp"

namespace {

constexpr VkExtent2D kPanelExtent{200, 100};
constexpr float kFrameSeconds = 1.0f / 90.0f;
constexpr float kSlideRate = 4.0f;

class Interaction : public testing::Test {
  protected:
    auto SetUp() -> void override {
        *g_input = InputState();
        g_config->transform_values.clear();
        g_config->curve_values.clear();
        g_config->allow_sliding = true;
        g_config->grab_smoothing = false;
        g_config->pointer_smoothing = false;
        g_input->SetHmd(Eigen::Affine3f::Identity());
        m_overlays = OverlayContainer::CreateEmpty();
        AimAway(1);
    }

    auto AddPanel(std::string name, const Eigen::Vector3f& position, std::shared_ptr<std::vector<RecordedEvent>> events)
        -> OverlayId {
        OverlayState state;
        state.SetName(std::move(name));
        state.SetWantVisible(true);
        state.SetInteractable(true);
        state.SetGrabbable(true);
        state.SetRecenter(true);
        state.SetShowHide(true);
        state.SetTransform(Eigen::Translation3f(position) * Eigen::Affine3f::Identity());
        state.SetInteractionTransform(OverlayState::UiTransform(kPanelExtent));
        const auto id = state.Id();
        m_overlays->Add(
            std::make_unique<Overlay>(std::move(state), std::make_unique<RecordingBackend>(kPanelExtent, std::move(events)))
        );
        return id;
    }

    auto Aim(size_t pointer, const Eigen::Vector3f& origin) -> void {
        g_input->Pointers()[pointer].SetPose(Eigen::Translation3f(origin) * Eigen::Affine3f::Identity());
    }

    auto AimAway(size_t pointer) -> void {
        g_input->Pointers()[pointer].SetPose(
            Eigen::Affine3f(Eigen::AngleAxisf(std::numbers::pi_v<float>, Eigen::Vector3f::UnitY()))
        );
    }

    auto Frame(const PointerState& left, const PointerState& right = {}) -> std::array<InteractionResult, 2> {
        g_input->PreUpdate();
        g_input->SetDeltaSeconds(kFrameSeconds);
        g_input->Pointers()[0].SetNow(left);
        g_input->Pointers()[1].SetNow(right);
        g_input->PostUpdate();
        return InputState::Interact();
    }

    auto Pointer(size_t index) -> ::Pointer& { return g_input->Pointers()[index]; }

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
                                value.modify(overlay->State());
                            }
                        } else if constexpr (std::is_same_v<TaskType, DropOverlayTask>) {
                            m_overlays->Remove(value.selector);
                        } else if constexpr (std::is_same_v<TaskType, CreateOverlayTask>) {
                            if (m_overlays->BySelector(value.selector) == nullptr) {
                                if (auto made = value.create()) {
                                    m_overlays->Add(std::make_unique<Overlay>(std::move(made->first), std::move(made->second)));
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
        const auto corners = OverlayLayout::Corners(*m_overlays->ById(id));
        EXPECT_TRUE(corners.has_value());
        return corners ? OverlayLayout::ViewBounds(*corners, g_input->Hmd()) : ViewRect{};
    }

    std::unique_ptr<OverlayContainer> m_overlays;
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

TEST_F(Interaction, HitsOutsideUvRangeAreIgnored) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);

    Aim(0, {0.6f, 0.0f, 0.0f});
    const auto result = Frame({});
    EXPECT_EQ(result[0].length, 0.0f);
    EXPECT_FALSE(Pointer(0).Interaction().hovered_id.has_value());
    EXPECT_TRUE(events->empty());
}

TEST_F(Interaction, ClickPressAndReleaseAreDelivered) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());

    Frame({});
    Frame({.click = true});
    EXPECT_EQ(Pointer(0).Interaction().clicked_id, id);
    Frame({});
    EXPECT_FALSE(Pointer(0).Interaction().clicked_id.has_value());

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

TEST_F(Interaction, ReleasingTheTriggerWhileGrabbingReleasesTheClick) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());

    Frame({.click = true});
    Frame({.click = true, .grab = true});
    ASSERT_TRUE(Pointer(0).Interaction().grabbed.has_value());
    Frame({.grab = true});
    EXPECT_FALSE(Pointer(0).Interaction().clicked_id.has_value());
    Frame({});

    const auto clicks = Events(events, RecordedKind::Pointer);
    ASSERT_EQ(clicks.size(), 2U);
    EXPECT_TRUE(clicks[0].pressed);
    EXPECT_FALSE(clicks[1].pressed);
}

TEST_F(Interaction, ScreenReleasesTheButtonItPressed) {
    ScopedRecordingHid hid;
    ScreenInteraction screen(Eigen::Vector2f::Zero(), Eigen::Vector2f(100.0f, 100.0f), FrameTransform::Normal);

    screen.OnPointer({.pointer = 0, .mode = PointerMode::Right}, true);
    screen.OnPointer({.pointer = 0, .mode = PointerMode::Left}, false);

    const std::vector<ButtonEvent> expected{{.button = kMouseRight, .down = true}, {.button = kMouseRight, .down = false}};
    EXPECT_EQ(hid.Buttons(), expected);
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

TEST_F(Interaction, RecentlyClickedRightPointerGoesFirstAndLowerIndexTakesPrimary) {
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

TEST_F(Interaction, GrabFollowsControllerRotationFreely) {
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
    const Eigen::Affine3f expected = Compose(turned, Eigen::Affine3f(Eigen::Translation3f(0.0f, 0.0f, -2.0f)));
    EXPECT_TRUE(overlay->State().Transform().matrix().isApprox(expected.matrix(), 1e-4f));
}

TEST_F(Interaction, DropRemembersPositionRelativeToHead) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());
    g_input->SetHmd(Pose(Eigen::Vector3f(0.0f, 1.6f, 0.0f), Eigen::Quaternionf(Eigen::AngleAxisf(0.4f, Eigen::Vector3f::UnitY()))));

    Frame({.grab = true});
    Aim(0, {0.3f, 0.0f, 0.0f});
    Frame({.grab = true});
    Frame({});

    const auto* overlay = m_overlays->ById(id);
    ASSERT_TRUE(overlay->State().SavedTransform().has_value());
    const Eigen::Affine3f expected = Compose(Eigen::Affine3f(g_input->Hmd().matrix().inverse()), overlay->State().Transform());
    EXPECT_TRUE(overlay->State().SavedTransform()->matrix().isApprox(expected.matrix(), 1e-4f));
    const auto saved = std::ranges::find(
        g_config->transform_values,
        std::string("panel"),
        &std::pair<std::string, Eigen::Affine3f>::first
    );
    ASSERT_NE(saved, g_config->transform_values.end());
    EXPECT_TRUE(saved->second.matrix().isApprox(expected.matrix(), 1e-4f));
}

TEST_F(Interaction, StickPushesAwayAndScalesWithDistance) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());

    const float step = std::exp(kSlideRate * kFrameSeconds);
    Frame({.grab = true});
    Frame({.scroll_y = 1.0f, .grab = true});
    const auto* overlay = m_overlays->ById(id);
    const Eigen::Matrix3f pushed = overlay->State().Transform().linear();
    EXPECT_NEAR(overlay->State().Transform().translation().z(), -2.0f * step, 1e-5f);
    EXPECT_NEAR(pushed.col(0).norm(), step, 1e-5f);

    Frame({.scroll_y = -1.0f, .grab = true});
    Frame({.scroll_y = -1.0f, .click = true, .grab = true});
    const Eigen::Matrix3f pulled = overlay->State().Transform().linear();
    EXPECT_NEAR(overlay->State().Transform().translation().z(), -2.0f / step, 1e-5f);
    EXPECT_NEAR(pulled.col(0).norm(), 1.0f / step, 1e-5f);
    EXPECT_NE(Pointer(0).Interaction().mode, PointerMode::Special);
}

TEST_F(Interaction, StickScalesWithDistanceFromTheHead) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, {0.0f, 0.0f, -0.5f});

    const float pushed_to = std::fma(1.5f, std::exp(kSlideRate * kFrameSeconds), 0.5f);
    Frame({.grab = true});
    Frame({.scroll_y = 1.0f, .grab = true});
    const auto* overlay = m_overlays->ById(id);
    const Eigen::Matrix3f pushed = overlay->State().Transform().linear();
    EXPECT_NEAR(overlay->State().Transform().translation().z(), -pushed_to, 1e-5f);
    EXPECT_NEAR(pushed.col(0).norm(), pushed_to / 2.0f, 1e-5f);
}

TEST_F(Interaction, StickSlidesFarOverlaysFasterThanNearOnes) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto near = AddPanel("near", {0.0f, 0.0f, -1.0f}, events);
    const auto far = AddPanel("far", {3.0f, 0.0f, -4.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());
    Frame({.grab = true});
    for (int frame = 0; frame < 45; ++frame) {
        Frame({.scroll_y = 1.0f, .grab = true});
    }
    Frame({});
    const float near_moved = m_overlays->ById(near)->State().Transform().translation().norm() - 1.0f;
    EXPECT_NEAR(near_moved, std::exp(kSlideRate * 0.5f) - 1.0f, 0.01f);

    Aim(0, Eigen::Vector3f::Zero());
    Pointer(0).Pose().linear() = Eigen::Matrix3f(Eigen::AngleAxisf(-std::atan2(3.0f, 4.0f), Eigen::Vector3f::UnitY()));
    Frame({.grab = true});
    ASSERT_TRUE(Pointer(0).Interaction().grabbed.has_value());
    EXPECT_EQ(Pointer(0).Interaction().grabbed->grabbed_id, far);
    for (int frame = 0; frame < 9; ++frame) {
        Frame({.scroll_y = 1.0f, .grab = true});
    }
    const float far_moved = m_overlays->ById(far)->State().Transform().translation().norm() - 5.0f;
    const float near_first_tenth = std::exp(kSlideRate * 0.1f) - 1.0f;
    EXPECT_NEAR(far_moved, 5.0f * near_first_tenth, 0.01f);
}

TEST_F(Interaction, StickDriftDoesNotMoveOrResize) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());

    Frame({.grab = true});
    for (int frame = 0; frame < 100; ++frame) {
        Frame({.scroll_y = frame % 2 == 0 ? 0.15f : -0.1f, .grab = true});
    }
    const auto* overlay = m_overlays->ById(id);
    const Eigen::Matrix3f linear = overlay->State().Transform().linear();
    EXPECT_TRUE(overlay->State().Transform().translation().isApprox(Eigen::Vector3f(0.0f, 0.0f, -2.0f), 1e-6f));
    EXPECT_NEAR(linear.col(0).norm(), 1.0f, 1e-6f);
}

TEST_F(Interaction, GrabSmoothingRemovesHandTremor) {
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
            wobble = std::max(wobble, std::abs(m_overlays->ById(id)->State().Transform().translation().x()));
        }
    }
    EXPECT_LT(wobble, 0.0005f);
}

TEST_F(Interaction, GrabSmoothingKeepsUpWithDeliberateMoves) {
    g_config->grab_smoothing = true;
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());
    Frame({.grab = true});

    for (int frame = 1; frame <= 30; ++frame) {
        Aim(0, {0.5f * static_cast<float>(frame) / 30.0f, 0.0f, 0.0f});
        Frame({.grab = true});
    }
    const float lag = 0.5f - m_overlays->ById(id)->State().Transform().translation().x();
    EXPECT_LT(lag, 0.05f);
    for (int frame = 0; frame < 30; ++frame) {
        Frame({.grab = true});
    }
    EXPECT_NEAR(m_overlays->ById(id)->State().Transform().translation().x(), 0.5f, 0.001f);
}

TEST_F(Interaction, ShowPlacesOverlaysRelativeToCurrentHead) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -1.0f}, events);
    auto* overlay = m_overlays->ById(id);
    overlay->State().SetWantVisible(false);
    overlay->State().SetSavedTransform(Eigen::Affine3f(Eigen::Translation3f(0.2f, -0.1f, -1.5f)));

    g_input->SetHmd(Eigen::Translation3f(0.0f, 1.6f, 0.0f) * Eigen::Affine3f::Identity());
    m_overlays->ShowHide();
    ASSERT_TRUE(overlay->State().WantVisible());
    EXPECT_TRUE(overlay->State().Transform().translation().isApprox(Eigen::Vector3f(0.2f, 1.5f, -1.5f), 1e-5f));

    m_overlays->ShowHide();
    EXPECT_FALSE(overlay->State().WantVisible());

    const Eigen::Affine3f turned = Pose(
        Eigen::Vector3f(1.0f, 1.6f, 2.0f),
        Eigen::Quaternionf(Eigen::AngleAxisf(std::numbers::pi_v<float> / 2.0f, Eigen::Vector3f::UnitY()))
    );
    g_input->SetHmd(turned);
    m_overlays->ShowHide();
    const Eigen::Affine3f expected = Compose(turned, *overlay->State().SavedTransform());
    EXPECT_TRUE(overlay->State().Transform().matrix().isApprox(expected.matrix(), 1e-5f));
    const Eigen::Vector3f to_overlay = overlay->State().Transform().translation() - turned.translation();
    EXPECT_NEAR(to_overlay.norm(), Eigen::Vector3f(0.2f, -0.1f, -1.5f).norm(), 1e-5f);
    const Eigen::Matrix3f overlay_linear = overlay->State().Transform().linear();
    const Eigen::Matrix3f head_linear = turned.linear();
    EXPECT_TRUE(overlay_linear.col(2).isApprox(head_linear.col(2), 1e-5f));
}

TEST_F(Interaction, ShowWhileLyingDownPlacesOverlayWhereYouLook) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -1.0f}, events);
    auto* overlay = m_overlays->ById(id);
    overlay->State().SetWantVisible(false);
    overlay->State().SetSavedTransform(Eigen::Affine3f(Eigen::Translation3f(0.0f, 0.0f, -1.0f)));

    g_input->SetHmd(Pose(
        Eigen::Vector3f(0.0f, 0.5f, 0.0f),
        Eigen::Quaternionf(Eigen::AngleAxisf(std::numbers::pi_v<float> / 2.0f, Eigen::Vector3f::UnitX()))
    ));
    m_overlays->ShowHide();
    EXPECT_TRUE(overlay->State().Transform().translation().isApprox(Eigen::Vector3f(0.0f, 1.5f, 0.0f), 1e-5f));
}

TEST_F(Interaction, RecenterMovesOnlyVisibleOverlays) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto shown = AddPanel("shown", {0.0f, 0.0f, -1.0f}, events);
    const auto hidden = AddPanel("hidden", {5.0f, 0.0f, -1.0f}, events);
    m_overlays->ById(hidden)->State().SetWantVisible(false);
    for (const auto id : {shown, hidden}) {
        m_overlays->ById(id)->State().SetSavedTransform(Eigen::Affine3f(Eigen::Translation3f(0.0f, 0.0f, -1.0f)));
    }

    g_input->SetHmd(Eigen::Translation3f(3.0f, 1.0f, 0.0f) * Eigen::Affine3f::Identity());
    m_overlays->Recenter();
    EXPECT_TRUE(m_overlays->ById(shown)->State().Transform().translation().isApprox(Eigen::Vector3f(3.0f, 1.0f, -1.0f), 1e-5f));
    EXPECT_TRUE(m_overlays->ById(hidden)->State().Transform().translation().isApprox(Eigen::Vector3f(5.0f, 0.0f, -1.0f), 1e-5f));
}

TEST_F(Interaction, DefaultLayoutArcsScreensAroundTheHead) {
    std::vector<ScreenOverlay> screens(3);
    const std::array<int32_t, 3> positions{1920, 0, 3840};
    for (size_t i = 0; i < screens.size(); ++i) {
        screens[i].state.SetName(std::format("screen{}", i));
        screens[i].state.SetSpawnScale(1.5f);
        screens[i].desktop_position = {positions[i], 0};
    }
    Screens::ArrangeInFrontOfHead(screens);

    const auto& middle = screens[0].state;
    EXPECT_NEAR(middle.SpawnPoint().x(), 0.0f, 1e-5f);
    EXPECT_LT(middle.SpawnPoint().y(), -0.1f);
    EXPECT_LT(middle.SpawnPoint().z(), -0.9f);
    EXPECT_LT(screens[1].state.SpawnPoint().x(), -0.5f);
    EXPECT_GT(screens[2].state.SpawnPoint().x(), 0.5f);
    for (const auto& screen : screens) {
        EXPECT_NEAR(screen.state.SpawnPoint().norm(), 1.0f, 1e-5f);
        const Eigen::Vector3f facing = screen.state.SpawnRotation() * Eigen::Vector3f::UnitZ();
        EXPECT_TRUE(facing.isApprox(-screen.state.SpawnPoint().normalized(), 1e-4f)) << screen.state.Name();
    }
}

TEST_F(Interaction, KeyboardSpawnsBelowViewFacingTheHead) {
    auto keyboard = Keyboard::Create(nullptr);
    ASSERT_TRUE(keyboard.has_value());
    const auto& state = (*keyboard)->State();
    EXPECT_FLOAT_EQ(state.SpawnPoint().x(), 0.0f);
    EXPECT_LT(state.SpawnPoint().y(), -0.3f);
    EXPECT_LT(state.SpawnPoint().z(), 0.0f);
    const Eigen::Vector3f facing = state.SpawnRotation() * Eigen::Vector3f::UnitZ();
    EXPECT_TRUE(facing.isApprox(-state.SpawnPoint().normalized(), 1e-4f));
}

TEST_F(Interaction, KeyboardUsesTheConfiguredProfile) {
    g_config->keyboard_profile = "full-size";
    auto keyboard = Keyboard::Create(nullptr);
    g_config->keyboard_profile = "keychron-v1-ansi";
    ASSERT_TRUE(keyboard.has_value());
    const auto meta = (*keyboard)->Backend()->Meta();
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->extent.width, (23U * 80U) + 28U);

    auto compact = Keyboard::Create(nullptr);
    ASSERT_TRUE(compact.has_value());
    EXPECT_NEAR((*compact)->State().SpawnScale() * 23.35f, (*keyboard)->State().SpawnScale() * 16.35f, 1e-4f);
}

TEST_F(Interaction, ProfileKeySwapsTheKeyboardInPlace) {
    auto created = Keyboard::Create(nullptr);
    ASSERT_TRUE(created.has_value());
    auto& original = (*created)->State();
    original.SetWantVisible(true);
    original.SetShowHide(true);
    original.SetTransform(Pose(Eigen::Vector3f(0.2f, -0.4f, -0.9f), Eigen::Quaternionf::Identity()));
    original.Transform().linear() *= original.SpawnScale();
    original.SetSavedTransform(original.Transform());
    const OverlayState before = original;
    m_overlays->Add(std::move(*created));
    g_config->transform_values = {{std::string(kKeyboardName), *before.SavedTransform()}};

    Keyboard::SwitchProfile(nullptr, "full-size");
    EXPECT_EQ(g_config->keyboard_profile, "full-size");
    RunTasks();

    auto* swapped = m_overlays->ByName(kKeyboardName);
    ASSERT_NE(swapped, nullptr);
    EXPECT_NE(swapped->State().Id(), before.Id());
    EXPECT_TRUE(swapped->State().WantVisible());
    EXPECT_TRUE(swapped->State().ShowHide());
    EXPECT_EQ(swapped->Backend()->Meta()->extent.width, (23U * 80U) + 28U);
    const float factor = 23.35f / 16.35f;
    const Eigen::Matrix3f linear = swapped->State().Transform().linear();
    const Eigen::Matrix3f linear_before = before.Transform().linear();
    EXPECT_TRUE(swapped->State().Transform().translation().isApprox(before.Transform().translation(), 1e-6f));
    EXPECT_NEAR(linear.col(0).norm(), linear_before.col(0).norm() * factor, 1e-5f);
    ASSERT_TRUE(swapped->State().SavedTransform().has_value());
    EXPECT_TRUE(g_config->transform_values.front().second.matrix().isApprox(swapped->State().SavedTransform()->matrix()));
    g_config->keyboard_profile = "keychron-v1-ansi";
}

TEST_F(Interaction, DroppingOntoAnotherOverlayKeepsTheOverlap) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto fixed = AddPanel("fixed", {0.0f, 0.0f, -2.0f}, events);
    const auto moving = AddPanel("moving", {0.3f, 0.0f, -2.0f}, events);

    Aim(0, {0.6f, 0.0f, 0.0f});
    Frame({.grab = true});
    ASSERT_TRUE(Pointer(0).Interaction().grabbed.has_value());
    Frame({});

    EXPECT_TRUE(m_overlays->ById(moving)->State().Transform().translation().isApprox(Eigen::Vector3f(0.3f, 0.0f, -2.0f), 1e-6f));
    EXPECT_TRUE(OverlayLayout::Overlaps(Bounds(moving), Bounds(fixed), 0.0f));
}

TEST_F(Interaction, ShowHideRestoresOnlyWhatWasOpen) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto open = AddPanel("open", {0.0f, 0.0f, -1.0f}, events);
    const auto closed = AddPanel("closed", {2.0f, 0.0f, -1.0f}, events);
    m_overlays->ById(closed)->State().SetWantVisible(false);

    m_overlays->ShowHide();
    EXPECT_FALSE(m_overlays->ById(open)->State().WantVisible());
    EXPECT_FALSE(m_overlays->ById(closed)->State().WantVisible());

    m_overlays->ShowHide();
    EXPECT_TRUE(m_overlays->ById(open)->State().WantVisible());
    EXPECT_FALSE(m_overlays->ById(closed)->State().WantVisible());
}

TEST_F(Interaction, ShowHideIncludesOverlaysOpenedOutsideTheSet) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto screen = AddPanel("screen", {0.0f, 0.0f, -1.0f}, events);
    const auto extra = AddPanel("extra", {2.0f, 0.0f, -1.0f}, events);
    m_overlays->ById(extra)->State().SetShowHide(false);

    m_overlays->ShowHide();
    EXPECT_FALSE(m_overlays->ById(screen)->State().WantVisible());
    EXPECT_FALSE(m_overlays->ById(extra)->State().WantVisible());

    m_overlays->ShowHide();
    EXPECT_TRUE(m_overlays->ById(screen)->State().WantVisible());
    EXPECT_TRUE(m_overlays->ById(extra)->State().WantVisible());
}

TEST_F(Interaction, ShowHideKeepsOverlapsTheUserMade) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto screen = AddPanel("screen", {0.0f, 0.0f, -1.0f}, events);
    const auto keyboard = AddPanel(std::string(kKeyboardName), {0.0f, -0.3f, -1.0f}, events);
    g_overlays->Screens().push_back(ScreenMeta{.name = "screen", .id = screen});
    m_overlays->ById(screen)->State().SetSavedTransform(m_overlays->ById(screen)->State().Transform());
    ASSERT_TRUE(OverlayLayout::Overlaps(Bounds(keyboard), Bounds(screen), 0.0f));

    Aim(0, {0.0f, -0.45f, 0.0f});
    Frame({.grab = true});
    ASSERT_EQ(Pointer(0).Interaction().grabbed->grabbed_id, keyboard);
    Frame({});

    m_overlays->ShowHide();
    g_input->SetHmd(Eigen::Translation3f(0.0f, 1.6f, 0.0f) * Eigen::Affine3f::Identity());
    m_overlays->ShowHide();
    ASSERT_TRUE(m_overlays->ById(keyboard)->State().WantVisible());
    EXPECT_TRUE(m_overlays->ById(keyboard)->State().Transform().translation().isApprox(Eigen::Vector3f(0.0f, 1.3f, -1.0f), 1e-5f));
    EXPECT_TRUE(OverlayLayout::Overlaps(Bounds(keyboard), Bounds(screen), 0.0f));
}

TEST_F(Interaction, ShowHideClearsOverlapsTheUserDidNotMake) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto screen = AddPanel("screen", {0.0f, 0.0f, -1.0f}, events);
    const auto keyboard = AddPanel(std::string(kKeyboardName), {0.0f, -0.3f, -1.0f}, events);
    g_overlays->Screens().push_back(ScreenMeta{.name = "screen", .id = screen});
    for (const auto id : {screen, keyboard}) {
        auto& state = m_overlays->ById(id)->State();
        state.SetSavedTransform(state.Transform());
    }
    ASSERT_TRUE(OverlayLayout::Overlaps(Bounds(keyboard), Bounds(screen), 0.0f));

    m_overlays->ShowHide();
    m_overlays->ShowHide();
    ASSERT_TRUE(m_overlays->ById(keyboard)->State().WantVisible());
    EXPECT_TRUE(m_overlays->ById(screen)->State().Transform().translation().isApprox(Eigen::Vector3f(0.0f, 0.0f, -1.0f), 1e-6f));
    EXPECT_FALSE(OverlayLayout::Overlaps(Bounds(keyboard), Bounds(screen), 0.0f));
}

TEST_F(Interaction, DraggingAwayForgetsTheOverlap) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto screen = AddPanel("screen", {0.0f, 0.0f, -1.0f}, events);
    const auto keyboard = AddPanel(std::string(kKeyboardName), {0.0f, -0.3f, -1.0f}, events);
    g_overlays->Screens().push_back(ScreenMeta{.name = "screen", .id = screen});
    m_overlays->ById(screen)->State().SetSavedTransform(m_overlays->ById(screen)->State().Transform());

    Aim(0, {0.0f, -0.45f, 0.0f});
    Frame({.grab = true});
    Frame({});
    Frame({.grab = true});
    Aim(0, {0.0f, -0.95f, 0.0f});
    Frame({.grab = true});
    Frame({});
    ASSERT_FALSE(OverlayLayout::Overlaps(Bounds(keyboard), Bounds(screen), 0.0f));

    m_overlays->ById(keyboard)->State().SetSavedTransform(Eigen::Affine3f(Eigen::Translation3f(0.0f, -0.3f, -1.0f)));
    m_overlays->Recenter();
    EXPECT_FALSE(OverlayLayout::Overlaps(Bounds(keyboard), Bounds(screen), 0.0f));
}

TEST_F(Interaction, RecenterClearsOverlapsThatWereNotThereBefore) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto screen = AddPanel("screen", {0.0f, 0.0f, -1.0f}, events);
    const auto keyboard = AddPanel(std::string(kKeyboardName), {0.0f, -1.0f, -1.0f}, events);
    g_overlays->Screens().push_back(ScreenMeta{.name = "screen", .id = screen});
    m_overlays->ById(screen)->State().SetSavedTransform(Eigen::Affine3f(Eigen::Translation3f(0.0f, 0.0f, -1.0f)));
    m_overlays->ById(keyboard)->State().SetSavedTransform(Eigen::Affine3f(Eigen::Translation3f(0.0f, -0.3f, -1.0f)));
    ASSERT_FALSE(OverlayLayout::Overlaps(Bounds(keyboard), Bounds(screen), 0.0f));

    m_overlays->Recenter();
    EXPECT_TRUE(m_overlays->ById(screen)->State().Transform().translation().isApprox(Eigen::Vector3f(0.0f, 0.0f, -1.0f), 1e-6f));
    EXPECT_FALSE(OverlayLayout::Overlaps(Bounds(keyboard), Bounds(screen), 0.0f));
    EXPECT_NEAR(
        m_overlays->ById(keyboard)->State().Transform().translation().norm(),
        Eigen::Vector3f(0.0f, -0.3f, -1.0f).norm(),
        1e-4f
    );
}

TEST_F(Interaction, ShowKeepsKeyboardClearOfScreen) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto screen = AddPanel("screen", {0.0f, 0.0f, -1.0f}, events);
    const auto keyboard = AddPanel(std::string(kKeyboardName), {0.0f, 0.0f, -1.0f}, events);
    g_overlays->Screens().push_back(ScreenMeta{.name = "screen", .id = screen});
    const Eigen::Affine3f screen_spot(Eigen::Translation3f(0.0f, -0.1f, -1.0f));
    for (const auto id : {screen, keyboard}) {
        m_overlays->ById(id)->State().SetWantVisible(false);
    }
    m_overlays->ById(screen)->State().SetSavedTransform(screen_spot);
    m_overlays->ById(keyboard)->State().SetSavedTransform(Eigen::Affine3f(Eigen::Translation3f(0.0f, -0.4f, -1.0f)));

    m_overlays->ShowHide();
    ASSERT_TRUE(m_overlays->ById(keyboard)->State().WantVisible());
    EXPECT_TRUE(m_overlays->ById(screen)->State().Transform().matrix().isApprox(screen_spot.matrix(), 1e-6f));
    EXPECT_FALSE(OverlayLayout::Overlaps(Bounds(keyboard), Bounds(screen), 0.0f));
    EXPECT_LT(Bounds(keyboard).max_elevation, Bounds(screen).min_elevation);
}

TEST_F(Interaction, RecenterStepsAroundLockedOverlays) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto locked = AddPanel("locked", {0.0f, 0.0f, -1.0f}, events);
    const auto free = AddPanel("free", {3.0f, 0.0f, -1.0f}, events);
    m_overlays->ById(locked)->State().SetRecenter(false);
    m_overlays->ById(free)->State().SetSavedTransform(Eigen::Affine3f(Eigen::Translation3f(0.1f, 0.0f, -1.0f)));

    m_overlays->Recenter();
    EXPECT_TRUE(m_overlays->ById(locked)->State().Transform().translation().isApprox(Eigen::Vector3f(0.0f, 0.0f, -1.0f), 1e-6f));
    EXPECT_FALSE(OverlayLayout::Overlaps(Bounds(free), Bounds(locked), 0.0f));
    EXPECT_NEAR(m_overlays->ById(free)->State().Transform().translation().norm(), Eigen::Vector3f(0.1f, 0.0f, -1.0f).norm(), 1e-4f);
}

TEST_F(Interaction, FootprintDescribesTheEyeRelativeToTheScreen) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.5f, 0.0f, -2.0f}, events);
    const Eigen::Vector2f density(800.0f, 800.0f);

    const auto footprint = OverlayLayout::MeasureFootprint(*m_overlays->ById(id), g_input->Hmd(), density);
    ASSERT_TRUE(footprint.has_value());
    EXPECT_TRUE(footprint->eye.isApprox(Eigen::Vector3f(-0.5f, 0.0f, 2.0f), 1e-5f));
    EXPECT_TRUE(footprint->size.isApprox(Eigen::Vector2f(1.0f, 0.5f), 1e-5f));
    EXPECT_TRUE(footprint->display_pixels_per_radian.isApprox(density));
    EXPECT_FALSE(OverlayLayout::MeasureFootprint(*m_overlays->ById(id), g_input->Hmd(), Eigen::Vector2f::Zero()).has_value());
}

TEST_F(Interaction, GrabbedLaserStaysOnTheGrabbedPoint) {
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
    const Eigen::Affine3f line = Pointer::LinePose(Pointer(0).Pose(), moved[0].target);
    const Eigen::Matrix3f line_linear = line.linear();
    EXPECT_TRUE((line_linear * -Eigen::Vector3f::UnitZ()).isApprox(-Eigen::Vector3f::UnitZ(), 1e-5f));
    EXPECT_TRUE(m_overlays->ById(id)->State().Transform().translation().isApprox(Eigen::Vector3f(0.3f, 0.2f, -2.0f), 1e-6f));

    const auto released = Frame({});
    EXPECT_FALSE(released[0].target.has_value());
}

TEST_F(Interaction, PointerSmoothingSteadiesTheLaser) {
    g_config->pointer_smoothing = true;
    Aim(0, Eigen::Vector3f::Zero());
    Frame({});
    Frame({});
    float drift = 0.0f;
    for (int i = 0; i < 30; ++i) {
        const float jitter = (i % 2 == 0 ? 1.0f : -1.0f) * 0.004f;
        g_input->Pointers()[0].SetPose(Eigen::Affine3f(Eigen::AngleAxisf(jitter, Eigen::Vector3f::UnitY())));
        Frame({});
        const Eigen::Matrix3f linear = Pointer(0).Pose().linear();
        drift = std::max(drift, std::abs((linear * -Eigen::Vector3f::UnitZ()).x()));
    }
    EXPECT_LT(drift, 0.0015f);

    for (int i = 0; i < 30; ++i) {
        g_input->Pointers()[0].SetPose(Eigen::Affine3f(Eigen::AngleAxisf(0.5f, Eigen::Vector3f::UnitY())));
        Frame({});
    }
    const Eigen::Matrix3f settled = Pointer(0).Pose().linear();
    EXPECT_NEAR(std::asin(-(settled * -Eigen::Vector3f::UnitZ()).x()), 0.5f, 0.01f);
    g_config->pointer_smoothing = false;
}

TEST_F(Interaction, HandAttachedOverlaysIgnorePointerSmoothing) {
    g_config->pointer_smoothing = true;
    Frame({});
    g_input->Pointers()[0].SetPose(Eigen::Affine3f(Eigen::Translation3f(0.3f, 0.1f, -0.2f)));
    Frame({});
    OverlayState watch;
    watch.SetAttachedTo(RelativeTo::LeftHand);
    watch.AutoMovement();
    EXPECT_TRUE(watch.Transform().translation().isApprox(Eigen::Vector3f(0.3f, 0.1f, -0.2f) + watch.SpawnPoint(), 1e-5f));
    EXPECT_FALSE(Pointer(0).Pose().translation().isApprox(Eigen::Vector3f(0.3f, 0.1f, -0.2f), 1e-3f));
    g_config->pointer_smoothing = false;
}

TEST_F(Interaction, ScrollWithOtherHandWhileGrabbedCurves) {
    auto events = std::make_shared<std::vector<RecordedEvent>>();
    const auto id = AddPanel("panel", {0.0f, 0.0f, -2.0f}, events);
    Aim(0, Eigen::Vector3f::Zero());
    Aim(1, {0.1f, 0.0f, 0.0f});

    Frame({.grab = true});
    Frame({.grab = true}, {.scroll_y = -10.0f});

    auto* overlay = m_overlays->ById(id);
    ASSERT_TRUE(overlay->State().Curvature().has_value());
    EXPECT_NEAR(*overlay->State().Curvature(), 0.1f, 1e-5f);
    EXPECT_TRUE(Events(events, RecordedKind::Scroll).empty());

    Frame({.grab = true}, {.scroll_y = 20.0f});
    EXPECT_FALSE(overlay->State().Curvature().has_value());

    Frame({}, {});
    EXPECT_TRUE(std::ranges::none_of(g_config->curve_values, [](const auto& entry) -> bool { return entry.first == "panel"; }));
}

TEST_F(Interaction, ModularButtonClickRunsItsAction) {
    const auto parsed = Yaml::Parse(R"(
width: 0.3
form:
  width: 200
  height: 100
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
    auto canvas = Modular::CreateCanvas(config);
    ASSERT_TRUE(canvas.has_value());

    OverlayState state;
    state.SetName("buttons");
    state.SetWantVisible(true);
    state.SetInteractable(true);
    state.SetTransform(Eigen::Translation3f(0.0f, 0.0f, -1.0f) * Eigen::Affine3f::Identity());
    state.SetInteractionTransform(OverlayState::UiTransform(kPanelExtent));
    m_overlays->Add(std::make_unique<Overlay>(std::move(state), std::move(*canvas)));

    const bool before = g_config->notifications_enabled;
    Aim(0, Eigen::Vector3f::Zero());
    Frame({});
    Frame({.click = true});
    EXPECT_EQ(g_config->notifications_enabled, before);
    Frame({});
    EXPECT_EQ(g_config->notifications_enabled, !before);
    g_config->notifications_enabled = before;
}
