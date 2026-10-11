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

#include <cstdlib>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <numbers>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "Button.hpp"
#include "CommandBuffer.hpp"
#include "Config.hpp"
#include "Device.hpp"
#include "Editor.hpp"
#include "EditorPanel.hpp"
#include "GpuReadback.hpp"
#include "Keyboard.hpp"
#include "Keymap.hpp"
#include "OverlayContainer.hpp"
#include "OverlayLayout.hpp"
#include "Png.hpp"
#include "RecordingHid.hpp"
#include "State.hpp"
#include "TestApp.hpp"
#include "Theme.hpp"
#include "Watch.hpp"

namespace {

constexpr float kFrameSeconds = 1.0f / 90.0f;

class EditorWorkspace : public testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!ScratchConfig()) {
            GTEST_SKIP() << "the editor writes drafts, set XDG_CONFIG_HOME to a scratch directory";
        }
        app_ = &TestApp::Get();
        RemoveDrafts();
        app_->input = InputState();
        app_->config.grab_smoothing = false;
        app_->config.pointer_smoothing = false;
        app_->config.keyboard_profile = "keychron-v1-ansi";
        app_->input.hmd = Eigen::Affine3f::Identity();
        app_->text_input.reset();
        overlays_ = OverlayContainer::CreateEmpty();
        auto watch = Watch::Create(*app_);
        ASSERT_TRUE(watch.has_value());
        overlays_->Add(std::move(*watch));
        auto keyboard = Keyboard::Create(*app_, nullptr);
        ASSERT_TRUE(keyboard.has_value());
        overlays_->Add(std::move(*keyboard));
        AimAway(0);
        AimAway(1);
    }

    auto TearDown() -> void override {
        if (app_ == nullptr) {
            return;
        }
        app_->text_input.reset();
        overlays_.reset();
        RemoveDrafts();
        if (ScratchConfig()) {
            std::error_code error;
            std::filesystem::remove(Watch::UserFile("default"), error);
            std::filesystem::remove_all(Config::Root() / "watches", error);
            std::filesystem::remove_all(Theme::UserDirectory(), error);
            Theme::Reload();
            Theme::SetActive(Theme::Find("macchiato"));
            app_->config.theme = "macchiato";
            app_->config.watch_profile = "default";
            std::filesystem::remove(KeyboardProfiles::UserDirectory() / "keychron-v1-ansi-custom.yaml", error);
        }
        KeyboardProfiles::Reload();
        app_->config.keyboard_profile = "keychron-v1-ansi";
        std::deque<Task> drained;
        app_->tasks.TakeDue(drained);
    }

    static auto ScratchConfig() -> bool { return std::getenv("XDG_CONFIG_HOME") != nullptr; }

    static auto RemoveDrafts() -> void {
        std::error_code error;
        std::filesystem::remove(Editor::DraftFile(EditTarget::Watch), error);
        std::filesystem::remove(Editor::DraftFile(EditTarget::Keyboard), error);
    }

    auto Recreate() -> void {
        overlays_ = OverlayContainer::CreateEmpty();
        auto watch = Watch::Create(*app_);
        ASSERT_TRUE(watch.has_value());
        overlays_->Add(std::move(*watch));
        auto keyboard = Keyboard::Create(*app_, nullptr);
        ASSERT_TRUE(keyboard.has_value());
        overlays_->Add(std::move(*keyboard));
    }

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
                        } else if (std::holds_alternative<OpenEditorTask>(value)) {
                            overlays_->OpenEditor(*app_);
                        }
                    },
                    task
                );
            }
        }
    }

    auto Step() -> void {
        overlays_->UpdateEditor(*app_);
        RunTasks();
        for (const auto kind : Editor::kPanels) {
            if (auto* panel = Panel(kind)) {
                EXPECT_TRUE(panel->backend->ShouldRender(*app_).has_value());
            }
        }
    }

    auto Open() -> Editor& {
        overlays_->OpenEditor(*app_);
        RunTasks();
        Step();
        Step();
        auto& editor = *overlays_->ActiveEditor();
        editor.watch.hold = {};
        editor.keyboard.hold = {};
        return editor;
    }

    auto Frame(const PointerState& left, const PointerState& right = {}) -> void {
        app_->input.PreUpdate();
        app_->input.delta_seconds = kFrameSeconds;
        app_->input.pointers[0].now = left;
        app_->input.pointers[1].now = right;
        app_->input.PostUpdate(app_->config);
        Step();
        InputState::Interact(*app_, *overlays_);
        RunTasks();
    }

    auto Panel(PanelKind kind) -> Overlay* { return overlays_->ByName(Editor::PanelName(kind)); }

    auto AimAt(size_t pointer, PanelKind kind, const Eigen::Vector2f& uv) -> void {
        auto* panel = Panel(kind);
        ASSERT_NE(panel, nullptr);
        const auto corners = OverlayLayout::Corners(*panel);
        ASSERT_TRUE(corners.has_value());
        const auto& c = *corners;
        const Eigen::Vector3f point = c[0] + (c[1] - c[0]) * uv.x() + (c[3] - c[0]) * (1.0f - uv.y());
        const Eigen::Vector3f normal = (c[1] - c[0]).cross(c[3] - c[0]).normalized();
        const Eigen::Matrix3f linear = panel->state.transform.linear();
        Eigen::Affine3f pose = Eigen::Affine3f::Identity();
        pose.linear() = linear / linear.col(0).norm();
        pose.translation() = point + normal * 0.4f;
        app_->input.pointers[pointer].pose = pose;
    }

    auto AimAway(size_t pointer) -> void {
        app_->input.pointers[pointer].pose = Eigen::Affine3f(Eigen::AngleAxisf(std::numbers::pi_v<float>, Eigen::Vector3f::UnitY()));
    }

    auto RenderPanel(PanelKind kind, const std::string& name) -> void {
        auto* panel = Panel(kind);
        ASSERT_NE(panel, nullptr);
        auto& backend = *panel->backend;
        ASSERT_TRUE(backend.ShouldRender(*app_).has_value());
        const auto meta = backend.Meta();
        ASSERT_TRUE(meta.has_value());
        auto target = app_->device->CreateRenderTexture(
            meta->extent.width,
            meta->extent.height,
            app_->device->NativeFormat(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        );
        ASSERT_TRUE(target.has_value());
        CommandBatch batch;
        const auto rendered = backend.Render(*app_, *target, batch, 1.0f);
        ASSERT_TRUE(rendered.has_value() && *rendered);
        ASSERT_TRUE(batch.Submit().has_value());
        app_->device->GraphicsQueue().WaitIdle();
        const auto pixels = GpuReadback::Read(*app_->device, **target);
        EXPECT_GT(GpuReadback::DistinctColors(pixels), 2U);
        Png::Write(TestApp::OutputDir() / std::format("{}.png", name), meta->extent.width, meta->extent.height, pixels);
    }

    auto LiveWatchControls() -> size_t {
        auto* watch = overlays_->ByName(kWatchName);
        auto* canvas = dynamic_cast<Canvas*>(watch->backend.get());
        return canvas != nullptr ? canvas->ControlCount() : 0;
    }

    AppState* app_ = nullptr;
    std::unique_ptr<OverlayContainer> overlays_;
};

} // namespace

TEST_F(EditorWorkspace, TheWatchButtonOpensAWorkspaceAroundYou) {
    ActionRunner runner;
    runner.Run(*app_, SystemButtonAction{.action = SystemAction::Edit});
    RunTasks();
    ASSERT_NE(overlays_->ActiveEditor(), nullptr);
    Step();

    for (const auto kind : Editor::kPanels) {
        ASSERT_NE(Panel(kind), nullptr) << Editor::PanelName(kind);
    }
    const Eigen::Vector3f preview = Panel(PanelKind::Preview)->state.transform.translation();
    EXPECT_NEAR(preview.x(), 0.0f, 1e-4f);
    EXPECT_LT(preview.z(), -0.5f);
    EXPECT_GT(Panel(PanelKind::Toolbar)->state.transform.translation().y(), preview.y());
    EXPECT_LT(Panel(PanelKind::Palette)->state.transform.translation().x(), -0.2f);
    EXPECT_GT(Panel(PanelKind::Properties)->state.transform.translation().x(), 0.2f);
    EXPECT_TRUE(Panel(PanelKind::Preview)->state.want_visible);
    EXPECT_FALSE(Panel(PanelKind::Dialog)->state.want_visible);
    EXPECT_FALSE(Panel(PanelKind::Ghost)->state.want_visible);
    EXPECT_EQ(Panel(PanelKind::Preview)->state.relative_to, RelativeTo::Stage);
}

TEST_F(EditorWorkspace, TheWorkspaceOpensWhereverYouLook) {
    app_->input.hmd = Eigen::Translation3f(1.0f, 1.6f, 0.0f)
                    * Eigen::AngleAxisf(std::numbers::pi_v<float> / 2.0f, Eigen::Vector3f::UnitY());
    Open();
    const Eigen::Vector3f preview = Panel(PanelKind::Preview)->state.transform.translation();
    EXPECT_LT(preview.x(), 0.5f);
    EXPECT_NEAR(preview.z(), 0.0f, 1e-3f);
    EXPECT_NEAR(preview.y(), 1.6f - 0.08f, 1e-3f);
}

TEST_F(EditorWorkspace, GrabbingAnyPanelCarriesTheWholeWorkspace) {
    Open();
    const Eigen::Vector3f before = Panel(PanelKind::Preview)->state.transform.translation();
    AimAt(1, PanelKind::Toolbar, {0.5f, 0.5f});
    Frame({}, {.grab = true});
    ASSERT_TRUE(app_->input.pointers[1].interaction.grabbed.has_value());
    app_->input.pointers[1].pose.translation() += Eigen::Vector3f(0.2f, 0.0f, 0.0f);
    Frame({}, {.grab = true});
    Frame({}, {.grab = true});
    const Eigen::Vector3f after = Panel(PanelKind::Preview)->state.transform.translation();
    EXPECT_TRUE(after.isApprox(before + Eigen::Vector3f(0.2f, 0.0f, 0.0f), 1e-3f)) << after.transpose();
}

TEST_F(EditorWorkspace, DraggingFromThePaletteDropsOntoThePreview) {
    auto& editor = Open();
    const auto elements = editor.watch.Document().elements.size();
    const auto controls = LiveWatchControls();

    AimAt(1, PanelKind::Palette, {0.5f, 121.0f / 420.0f});
    Frame({});
    Frame({}, {.click = true});
    ASSERT_TRUE(editor.Carrying().has_value());
    EXPECT_TRUE(Panel(PanelKind::Ghost)->state.want_visible || (Step(), Panel(PanelKind::Ghost)->state.want_visible));

    AimAt(1, PanelKind::Preview, {0.5f, 0.4f});
    Frame({}, {.click = true});
    Frame({});
    EXPECT_FALSE(editor.Carrying().has_value());
    ASSERT_EQ(editor.watch.Document().elements.size(), elements + 1);
    EXPECT_TRUE(std::holds_alternative<ButtonElement>(editor.watch.Document().elements.back()));
    EXPECT_EQ(editor.watch.selected, elements);
    Step();
    EXPECT_GT(LiveWatchControls(), controls);
    EXPECT_TRUE(editor.Dirty());

    const auto lines = overlays_->EditorLines();
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_GT(lines.front().length, 0.05f);
}

TEST_F(EditorWorkspace, ClickingAPaletteItemAddsItAndReleasingInTheAirCancels) {
    auto& editor = Open();
    const auto elements = editor.watch.Document().elements.size();
    AimAt(1, PanelKind::Palette, {0.5f, 121.0f / 420.0f});
    Frame({});
    Frame({}, {.click = true});
    Frame({});
    EXPECT_EQ(editor.watch.Document().elements.size(), elements + 1);

    Frame({}, {.click = true});
    ASSERT_TRUE(editor.Carrying().has_value());
    AimAway(1);
    Frame({}, {.click = true});
    Frame({});
    EXPECT_FALSE(editor.Carrying().has_value());
    EXPECT_EQ(editor.watch.Document().elements.size(), elements + 1);
}

TEST_F(EditorWorkspace, ThePreviewMovesElementsWithThePointer) {
    auto& editor = Open();
    editor.watch.snap = false;
    const auto& config = editor.watch.Document();
    const auto index = config.elements.size() - 1;
    const auto rect = editor.watch.VisualRect(index);
    const Eigen::Vector2f size = editor.PanelSize(PanelKind::Preview);
    const auto uv = [&](float x, float y) -> Eigen::Vector2f { return editor.watch.ToPreview({x, y}).cwiseQuotient(size); };

    AimAt(1, PanelKind::Preview, uv(rect.x + rect.w / 2.0f, rect.y + rect.h / 2.0f));
    Frame({});
    Frame({}, {.click = true});
    EXPECT_EQ(editor.watch.selected, index);
    AimAt(1, PanelKind::Preview, uv(rect.x + rect.w / 2.0f - 30.0f, rect.y + rect.h / 2.0f));
    Frame({}, {.click = true});
    Frame({});
    EXPECT_NEAR(editor.watch.VisualRect(index).x, rect.x - 30.0f, 0.5f);
    EXPECT_TRUE(editor.watch.history.CanUndo());
}

TEST_F(EditorWorkspace, TypingOnTheKeyboardFillsTheFocusedField) {
    auto& editor = Open();
    auto us = Keymap::FromNames("us", "");
    ASSERT_TRUE(us.has_value());
    auto state = std::make_shared<KeyboardState>();
    state->typing_keymap = std::make_shared<const Keymap>(std::move(*us));
    KeyControl h(state, VirtualKey::H);
    KeyControl i(state, VirtualKey::I);
    KeyControl backspace(state, VirtualKey::BackSpace);
    KeyControl enter(state, VirtualKey::Return);
    ModifierControl shift(state, kModShift);
    const auto tap = [this](Control& control) -> void {
        control.OnPress(*app_, PointerMode::Left);
        control.OnRelease(*app_);
    };

    std::string committed;
    editor.BeginText(*app_, "test", "", [&committed](Editor&, AppState&, const std::string& text) -> void { committed = text; });
    ScopedRecordingHid hid(*app_);
    tap(i);
    tap(backspace);
    tap(shift);
    tap(h);
    tap(i);
    ASSERT_TRUE(editor.Focused().has_value());
    EXPECT_EQ(editor.Focused()->buffer, "Hi");
    tap(enter);
    EXPECT_EQ(committed, "Hi");
    EXPECT_FALSE(editor.Focused().has_value());
    EXPECT_EQ(app_->text_input, nullptr);
    EXPECT_TRUE(hid.Events().empty());

    tap(h);
    EXPECT_EQ(hid.Events().size(), 2U);
}

TEST_F(EditorWorkspace, PressingAnotherPanelKeepsWhatWasTyped) {
    auto& editor = Open();
    const auto index = editor.watch.Document().elements.size() - 1;
    editor.watch.selected = index;
    editor.BeginText(*app_, "button.text", "", [index](Editor& owner, AppState&, const std::string& text) -> void {
        owner.EditWatch([index, &text](ModularUiConfig& config) -> void {
            std::get<ButtonElement>(config.elements.at(index)).text = text;
        });
    });
    editor.OnKey(*app_, VirtualKey::O, "O");
    editor.OnKey(*app_, VirtualKey::K, "k");
    AimAt(1, PanelKind::Toolbar, {0.5f, 0.5f});
    Frame({});
    Frame({}, {.click = true});
    EXPECT_FALSE(editor.Focused().has_value());
    EXPECT_EQ(std::get<ButtonElement>(editor.watch.Document().elements.at(index)).text, "Ok");
    Frame({});
}

TEST_F(EditorWorkspace, PressingAKeyAssignsIt) {
    auto& editor = Open();
    editor.SwitchTarget(EditTarget::Keyboard);
    editor.keyboard.AddKey(0.0f, editor.keyboard.Document().rows - 1);
    editor.keyboard.selected.reset();
    editor.keyboard.history.Edit([](KeyboardLayout& layout) -> void { layout.rows += 1; });
    editor.keyboard.AddKey(0.0f, editor.keyboard.Document().rows - 1);
    const auto index = *editor.keyboard.selected;

    auto state = std::make_shared<KeyboardState>();
    KeyControl z(state, VirtualKey::Z);
    editor.BeginCapture(*app_, false, [index](Editor& owner, const std::vector<VirtualKey>& keys) -> void {
        owner.EditKeyboard([index, &keys](KeyboardLayout& layout) -> void {
            layout.keys.at(index).action = TypeKey{.key = keys.back()};
        });
    });
    EXPECT_TRUE(app_->text_input->WantsRawKeys());
    z.OnPress(*app_, PointerMode::Left);
    z.OnRelease(*app_);
    const auto* typed = std::get_if<TypeKey>(&editor.keyboard.Document().keys.at(index).action);
    ASSERT_NE(typed, nullptr);
    EXPECT_EQ(typed->key, VirtualKey::Z);
    EXPECT_FALSE(editor.Capturing());
}

TEST_F(EditorWorkspace, SaveWritesTheWatchAndRevertRemovesIt) {
    if (!ScratchConfig()) {
        GTEST_SKIP() << "writes config files, set XDG_CONFIG_HOME to a scratch directory";
    }
    auto& editor = Open();
    const float width = editor.watch.Document().width;
    editor.EditWatch([](ModularUiConfig& config) -> void { config.width += 0.01f; });
    editor.Save(*app_);
    ASSERT_TRUE(std::filesystem::exists(Watch::UserFile("default")));
    EXPECT_NEAR(Watch::LoadConfig("default").width, width + 0.01f, 1e-5f);
    EXPECT_FALSE(editor.Dirty());

    editor.Revert(*app_);
    EXPECT_TRUE(std::filesystem::exists(Watch::UserFile("default")));
    editor.Revert(*app_);
    EXPECT_FALSE(std::filesystem::exists(Watch::UserFile("default")));
    EXPECT_NEAR(editor.watch.Document().width, width, 1e-5f);
}

TEST_F(EditorWorkspace, SavingAKeyboardAddsAProfileAndSwitchesToIt) {
    if (!ScratchConfig()) {
        GTEST_SKIP() << "writes config files, set XDG_CONFIG_HOME to a scratch directory";
    }
    auto& editor = Open();
    editor.SwitchTarget(EditTarget::Keyboard);
    editor.keyboard.AddColumn();
    editor.SaveAsNew(*app_);
    EXPECT_EQ(editor.keyboard.Document().name, "keychron-v1-ansi-custom");
    EXPECT_TRUE(std::filesystem::exists(KeyboardProfiles::UserDirectory() / "keychron-v1-ansi-custom.yaml"));
    const auto& saved = KeyboardProfiles::Find("keychron-v1-ansi-custom");
    EXPECT_EQ(saved.name, "keychron-v1-ansi-custom");
    EXPECT_FLOAT_EQ(saved.columns, 17.0f);
    EXPECT_EQ(app_->config.keyboard_profile, "keychron-v1-ansi-custom");
    RunTasks();
    const auto meta = overlays_->ByName(kKeyboardName)->backend->Meta();
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->extent.width, (17U * 80U) + 28U);
    EXPECT_FALSE(editor.Dirty());
}

TEST_F(EditorWorkspace, DoneAsksBeforeDroppingUnsavedWork) {
    auto& editor = Open();
    const auto controls = LiveWatchControls();
    editor.watch.Insert(FormPart::Panel, std::nullopt);
    editor.EditWatch([](ModularUiConfig&) -> void {});
    Step();
    EXPECT_GT(LiveWatchControls(), controls);

    editor.Done(*app_);
    EXPECT_FALSE(editor.Closing());
    editor.Discard(*app_);
    EXPECT_TRUE(editor.Closing());
    Step();
    EXPECT_EQ(overlays_->ActiveEditor(), nullptr);
    for (const auto kind : Editor::kPanels) {
        EXPECT_EQ(Panel(kind), nullptr);
    }
    EXPECT_EQ(LiveWatchControls(), controls);
}

TEST_F(EditorWorkspace, LayersSpreadInDepthAndLetYouReachThrough) {
    auto& editor = Open();
    editor.SetExploded(true);
    Step();
    EXPECT_FALSE(Panel(PanelKind::Preview)->state.want_visible);
    const float back = Panel(PanelKind::BackLayer)->state.transform.translation().z();
    const float front = Panel(PanelKind::FrontLayer)->state.transform.translation().z();
    EXPECT_TRUE(Panel(PanelKind::MiddleLayer)->state.want_visible);
    EXPECT_NEAR(front - back, 2.0f * Editor::kLayerSpacing, 1e-4f);

    const Eigen::Vector2f size = editor.PanelSize(PanelKind::Preview);
    const auto uv = [&](float x, float y) -> Eigen::Vector2f { return editor.watch.ToPreview({x, y}).cwiseQuotient(size); };
    AimAt(1, PanelKind::FrontLayer, uv(150.0f, 150.0f));
    Frame({});
    Frame({}, {.click = true});
    Frame({});
    ASSERT_TRUE(editor.watch.selected.has_value());
    EXPECT_TRUE(std::holds_alternative<PanelElement>(editor.watch.Document().elements.at(*editor.watch.selected)));
    EXPECT_EQ(app_->input.pointers[1].interaction.hovered_id, Panel(PanelKind::BackLayer)->state.id);

    AimAt(1, PanelKind::FrontLayer, uv(350.0f, 68.0f));
    Frame({});
    Frame({}, {.click = true});
    Frame({});
    ASSERT_TRUE(editor.watch.selected.has_value());
    EXPECT_TRUE(std::holds_alternative<ButtonElement>(editor.watch.Document().elements.at(*editor.watch.selected)));
    EXPECT_EQ(app_->input.pointers[1].interaction.hovered_id, Panel(PanelKind::FrontLayer)->state.id);

    RenderPanel(PanelKind::BackLayer, "editor-layer-back");
    RenderPanel(PanelKind::MiddleLayer, "editor-layer-middle");
    RenderPanel(PanelKind::FrontLayer, "editor-layer-front");
}

TEST_F(EditorWorkspace, OnlyOneHandUsesTheEditorAtATime) {
    auto& editor = Open();
    editor.watch.snap = false;
    const auto index = editor.watch.Document().elements.size() - 1;
    const auto rect = editor.watch.VisualRect(index);
    const Eigen::Vector2f size = editor.PanelSize(PanelKind::Preview);
    const auto uv = [&](float x, float y) -> Eigen::Vector2f { return editor.watch.ToPreview({x, y}).cwiseQuotient(size); };

    AimAt(1, PanelKind::Preview, uv(rect.x + rect.w / 2.0f, rect.y + rect.h / 2.0f));
    AimAt(0, PanelKind::Preview, uv(60.0f, 60.0f));
    Frame({});
    Frame({}, {.click = true});
    EXPECT_EQ(editor.Owner(), 1U);
    EXPECT_EQ(editor.watch.selected, index);

    Frame({.click = true}, {.click = true});
    AimAt(0, PanelKind::Preview, uv(20.0f, 20.0f));
    Frame({.click = true}, {.click = true});
    EXPECT_EQ(editor.watch.selected, index);
    EXPECT_NEAR(editor.watch.VisualRect(index).x, rect.x, 0.5f);

    AimAt(1, PanelKind::Preview, uv(rect.x + rect.w / 2.0f - 20.0f, rect.y + rect.h / 2.0f));
    Frame({.click = true}, {.click = true});
    Frame({.click = true});
    EXPECT_NEAR(editor.watch.VisualRect(index).x, rect.x - 20.0f, 0.5f);
    EXPECT_FALSE(editor.Owner().has_value());
    Frame({});
}

TEST_F(EditorWorkspace, RecenterAndShowHideIncludeTheWorkspace) {
    Open();
    app_->input.hmd = Eigen::Affine3f(Eigen::AngleAxisf(std::numbers::pi_v<float> / 2.0f, Eigen::Vector3f::UnitY()));
    overlays_->Recenter(*app_);
    Step();
    EXPECT_LT(Panel(PanelKind::Preview)->state.transform.translation().x(), -0.5f);

    overlays_->ShowHide(*app_);
    Step();
    EXPECT_FALSE(Panel(PanelKind::Preview)->state.want_visible);
    EXPECT_FALSE(Panel(PanelKind::Toolbar)->state.want_visible);
    overlays_->ShowHide(*app_);
    Step();
    EXPECT_TRUE(Panel(PanelKind::Preview)->state.want_visible);
    EXPECT_TRUE(Panel(PanelKind::Toolbar)->state.want_visible);
}

TEST_F(EditorWorkspace, TheColourWheelPicksLiveAndUndoesInOneStep) {
    auto& editor = Open();
    const auto index = editor.watch.Document().elements.size() - 1;
    const auto color = [&editor, index]() -> std::string {
        return std::get<ButtonElement>(editor.watch.Document().elements.at(index)).bg_color;
    };
    const auto before = color();
    editor.OpenColor({
        .title = "Colour",
        .get = [index](const Editor& owner) -> std::string {
            return std::get<ButtonElement>(owner.watch.Document().elements.at(index)).bg_color;
        },
        .set = [index](Editor& owner, const std::string& value) -> void {
            owner.EditWatch([index, &value](ModularUiConfig& config) -> void {
                std::get<ButtonElement>(config.elements.at(index)).bg_color = value;
            });
        },
        .value = 1.0f,
    });
    Step();
    const auto& wheel = editor.WheelRect();
    ASSERT_GT(wheel.w, 0.0f);
    const Eigen::Vector2f center(wheel.x + wheel.w / 2.0f, wheel.y + wheel.h / 2.0f);
    EXPECT_TRUE(editor.DialogPointer(*app_, center + Eigen::Vector2f(wheel.w / 2.0f, 0.0f), true));
    EXPECT_EQ(color(), "#ff0000");
    EXPECT_TRUE(editor.DialogHover(*app_, center));
    EXPECT_EQ(color(), "#ffffff");
    EXPECT_TRUE(editor.DialogPointer(*app_, center, false));
    RenderPanel(PanelKind::Dialog, "editor-color-wheel-bright");
    const auto& bar = editor.BrightnessRect();
    EXPECT_TRUE(editor.DialogPointer(*app_, {bar.x, bar.y + bar.h / 2.0f}, true));
    EXPECT_TRUE(editor.DialogPointer(*app_, {bar.x, bar.y + bar.h / 2.0f}, false));
    EXPECT_EQ(color(), "#000000");
    EXPECT_FALSE(editor.DialogPointer(*app_, {1.0f, 1.0f}, true));
    RenderPanel(PanelKind::Dialog, "editor-color-wheel");

    editor.Undo(*app_);
    EXPECT_EQ(color(), "#ffffff");
    editor.Undo(*app_);
    EXPECT_EQ(color(), before);
}

TEST_F(EditorWorkspace, ExtraFormsGrowTheLiveWatch) {
    auto& editor = Open();
    const auto width = overlays_->ByName(kWatchName)->backend->Meta()->extent.width;
    editor.watch.Insert(FormPart::Form, std::nullopt);
    editor.EditWatch([](ModularUiConfig&) -> void {});
    Step();
    EXPECT_GT(overlays_->ByName(kWatchName)->backend->Meta()->extent.width, width);
    RenderPanel(PanelKind::Preview, "editor-extra-form");
    RenderPanel(PanelKind::Properties, "editor-extra-form-properties");
}

TEST_F(EditorWorkspace, TimezonesAreSearchedAndPicked) {
    auto& editor = Open();
    const auto& elements = editor.watch.Document().elements;
    const auto clock = std::ranges::find_if(elements, [](const ModularElement& element) -> bool {
        const auto* label = std::get_if<LabelElement>(&element);
        return label != nullptr && std::holds_alternative<ClockLabelContent>(label->content);
    });
    ASSERT_NE(clock, elements.end());
    const auto index = static_cast<size_t>(clock - elements.begin());
    editor.watch.selected = index;
    editor.OpenTimezones(index);
    editor.BeginText(*app_, "timezone.search", "", [](Editor&, AppState&, const std::string&) -> void {});
    for (const auto* letter : {"h", "e", "l", "s"}) {
        editor.OnKey(*app_, VirtualKey::A, letter);
    }
    Step();
    RenderPanel(PanelKind::Dialog, "editor-timezones");

    auto* dialog = dynamic_cast<EditorPanel*>(Panel(PanelKind::Dialog)->backend.get());
    ASSERT_NE(dialog, nullptr);
    auto* canvas = dialog->CanvasForTest();
    ASSERT_NE(canvas, nullptr);
    Control* helsinki = nullptr;
    size_t results = 0;
    for (size_t i = 0; i < canvas->ControlCount(); ++i) {
        auto& control = canvas->ControlAt(i);
        if (control.text.starts_with("Europe/") || control.text.starts_with("Asia/") || control.text.starts_with("America/")) {
            ++results;
        }
        if (control.text == "Europe/Helsinki") {
            helsinki = &control;
        }
    }
    ASSERT_NE(helsinki, nullptr);
    EXPECT_LE(results, 3U);
    helsinki->OnRelease(*app_);
    const auto& content = std::get<LabelElement>(editor.watch.Document().elements.at(index)).content;
    const auto& zone = std::get<ClockLabelContent>(content).timezone;
    ASSERT_TRUE(zone.has_value());
    EXPECT_EQ(std::get<std::string>(*zone), "Europe/Helsinki");
    EXPECT_EQ(editor.Dialog(), DialogKind::None);
    EXPECT_EQ(app_->text_input, nullptr);
}

TEST_F(EditorWorkspace, TheEditButtonClosesTheEditorAndKeepsADraft) {
    auto& editor = Open();
    const auto controls = LiveWatchControls();
    const auto elements = editor.watch.Document().elements.size();
    editor.watch.Insert(FormPart::Panel, std::nullopt);
    editor.EditWatch([](ModularUiConfig&) -> void {});
    Step();
    EXPECT_GT(LiveWatchControls(), controls);

    overlays_->OpenEditor(*app_);
    Step();
    EXPECT_EQ(overlays_->ActiveEditor(), nullptr);
    EXPECT_TRUE(std::filesystem::exists(Editor::DraftFile(EditTarget::Watch)));
    EXPECT_EQ(LiveWatchControls(), controls);

    auto& reopened = Open();
    EXPECT_EQ(reopened.watch.Document().elements.size(), elements + 1);
    EXPECT_TRUE(reopened.Dirty());
    EXPECT_GT(LiveWatchControls(), controls);
    reopened.Undo(*app_);
    EXPECT_FALSE(reopened.Dirty());
    reopened.Redo(*app_);
    reopened.Save(*app_);
    EXPECT_FALSE(std::filesystem::exists(Editor::DraftFile(EditTarget::Watch)));
}

TEST_F(EditorWorkspace, DraftsSurviveACrash) {
    auto& editor = Open();
    editor.SwitchTarget(EditTarget::Keyboard);
    editor.keyboard.AddColumn();
    editor.watch.Insert(FormPart::Button, std::nullopt);
    editor.SaveDrafts(true);
    Recreate();

    auto& recovered = Open();
    EXPECT_FLOAT_EQ(recovered.keyboard.Document().columns, 17.0f);
    EXPECT_TRUE(recovered.keyboard.history.Dirty());
    EXPECT_TRUE(recovered.watch.history.Dirty());
    recovered.Discard(*app_);
    Step();
    EXPECT_FALSE(std::filesystem::exists(Editor::DraftFile(EditTarget::Watch)));
    EXPECT_FALSE(std::filesystem::exists(Editor::DraftFile(EditTarget::Keyboard)));
}

TEST_F(EditorWorkspace, CityClocksAreOneComponentMeasuredByWhatTheyShow) {
    auto& editor = Open();
    const auto& config = editor.watch.Document();
    size_t clocks = 0;
    for (size_t i = 0; i < config.elements.size(); ++i) {
        const auto* label = std::get_if<LabelElement>(&config.elements[i]);
        const auto* clock = label != nullptr ? std::get_if<ClockLabelContent>(&label->content) : nullptr;
        if (clock == nullptr || !clock->city_size) {
            continue;
        }
        ++clocks;
        const auto rect = editor.watch.VisualRect(i);
        const auto& placed = ModularElements::Rect(config.elements[i]);
        EXPECT_LT(
            rect.y,
            editor.watch.Form(0).y + placed[1] - static_cast<float>(label->font_size) - static_cast<float>(*clock->city_size)
        );
        EXPECT_GT(rect.w, 20.0f);
        EXPECT_LT(rect.w, 110.0f);
        EXPECT_LE(rect.x + rect.w, editor.watch.Form(0).x + editor.watch.Form(0).w);
        EXPECT_EQ(editor.watch.HitTest({rect.x + 5.0f, rect.y + 3.0f}).index, i);
    }
    EXPECT_EQ(clocks, 2U);
    EXPECT_FALSE(std::ranges::any_of(config.elements, [](const ModularElement& element) -> bool {
        const auto* label = std::get_if<LabelElement>(&element);
        return label != nullptr && std::holds_alternative<TimezoneLabelContent>(label->content);
    }));
}

TEST_F(EditorWorkspace, ThemesRecolourEverythingThatUsesThem) {
    auto& editor = Open();
    EXPECT_EQ(Canvas::ParseColor("@accent").value(), Canvas::ParseColor("#c6a0f6").value());
    editor.SwitchTarget(EditTarget::Theme);
    RenderPanel(PanelKind::Properties, "editor-theme-properties");
    editor.EditTheme([](ThemeDefinition& theme) -> void {
        for (auto& color : theme.colors) {
            if (color.name == "accent") {
                color.hex = "#00ff00";
            }
        }
        theme.rounding = Rounding::Square;
    });
    EXPECT_EQ(Canvas::ParseColor("@accent").value(), Canvas::ParseColor("#00ff00").value());
    EXPECT_EQ(Theme::Active().rounding, Rounding::Square);
    EXPECT_TRUE(editor.Dirty());

    editor.NewTheme(*app_);
    EXPECT_TRUE(editor.theme.Dirty());
    editor.Save(*app_);
    editor.NewTheme(*app_);
    EXPECT_EQ(editor.theme.Current().name, "my-theme");
    EXPECT_EQ(app_->config.theme, "my-theme");
    EXPECT_TRUE(std::filesystem::exists(Theme::UserDirectory() / "my-theme.yaml"));
    EXPECT_EQ(Theme::Find("my-theme").Lookup("accent"), std::optional<std::string>("#00ff00"));

    editor.SelectTheme(*app_, "latte");
    EXPECT_EQ(Canvas::ParseColor("@background").value(), Canvas::ParseColor("#eff1f5").value());
    Step();
    RenderPanel(PanelKind::Preview, "editor-theme-latte");
    editor.SelectTheme(*app_, "my-theme");
    editor.DeleteTheme(*app_);
    editor.DeleteTheme(*app_);
    EXPECT_FALSE(std::filesystem::exists(Theme::UserDirectory() / "my-theme.yaml"));
    EXPECT_EQ(editor.theme.Current().name, "macchiato");
    editor.Revert(*app_);
    editor.Revert(*app_);
    EXPECT_EQ(Canvas::ParseColor("@accent").value(), Canvas::ParseColor("#c6a0f6").value());
}

TEST_F(EditorWorkspace, WatchLayoutsAreChosenCreatedAndDeleted) {
    auto& editor = Open();
    const auto elements = editor.watch.Document().elements.size();
    editor.NewWatch(*app_);
    EXPECT_EQ(editor.WatchProfile(), "watch-2");
    EXPECT_EQ(app_->config.watch_profile, "watch-2");
    editor.watch.Insert(FormPart::Panel, std::nullopt);
    editor.Save(*app_);
    EXPECT_EQ(Watch::LoadConfig("watch-2").elements.size(), elements + 1);

    editor.SelectWatch(*app_, "default");
    EXPECT_EQ(editor.watch.Document().elements.size(), elements);
    editor.SelectWatch(*app_, "watch-2");
    EXPECT_EQ(editor.watch.Document().elements.size(), elements + 1);
    RenderPanel(PanelKind::Properties, "editor-watch-layouts");
    editor.RenameWatch(*app_, "mine");
    EXPECT_EQ(Watch::Profiles(), (std::vector<std::string>{"default", "mine"}));
    editor.DeleteWatch(*app_);
    editor.DeleteWatch(*app_);
    EXPECT_EQ(Watch::Profiles(), std::vector<std::string>{"default"});
    EXPECT_EQ(editor.WatchProfile(), "default");
}

TEST_F(EditorWorkspace, KeyboardsArePickedFromTheList) {
    auto& editor = Open();
    editor.SwitchTarget(EditTarget::Keyboard);
    editor.SelectKeyboard(*app_, "full-size");
    EXPECT_EQ(editor.keyboard.Document().name, "full-size");
    EXPECT_EQ(app_->config.keyboard_profile, "full-size");
    editor.keyboard.AddColumn();
    editor.SelectKeyboard(*app_, "keychron-v1-ansi");
    EXPECT_EQ(editor.keyboard.Document().name, "full-size");
    RenderPanel(PanelKind::Properties, "editor-keyboards");
}

TEST_F(EditorWorkspace, LongNamesAreShortenedAndTheEditorFollowsTheTheme) {
    auto& editor = Open();
    const std::string name = "a-really-long-watch-layout-name-that-never-fits";
    {
        std::filesystem::create_directories(Config::Root() / "watches");
        std::ofstream(Watch::UserFile(name)) << "width: 0.1\nform: {width: 100, height: 50}\nelements: []\n";
    }
    editor.Touch();
    Step();
    auto* properties = dynamic_cast<EditorPanel*>(Panel(PanelKind::Properties)->backend.get());
    ASSERT_NE(properties, nullptr);
    auto* canvas = properties->CanvasForTest();
    ASSERT_NE(canvas, nullptr);
    bool shortened = false;
    for (size_t i = 0; i < canvas->ControlCount(); ++i) {
        const auto& text = canvas->ControlAt(i).text;
        EXPECT_NE(text, name);
        shortened = shortened || (text.starts_with("a-really") && text.ends_with("..."));
    }
    EXPECT_TRUE(shortened);
    RenderPanel(PanelKind::Properties, "editor-long-names");

    editor.SwitchTarget(EditTarget::Theme);
    editor.SelectTheme(*app_, "latte");
    Step();
    RenderPanel(PanelKind::Toolbar, "editor-toolbar-latte");
    RenderPanel(PanelKind::Properties, "editor-theme-latte-properties");
    editor.SelectTheme(*app_, "macchiato");
}

TEST_F(EditorWorkspace, PanelsRender) {
    auto& editor = Open();
    editor.watch.selected = editor.watch.Document().elements.size() - 2;
    editor.Touch();
    RenderPanel(PanelKind::Preview, "editor-watch-preview");
    RenderPanel(PanelKind::Toolbar, "editor-toolbar");
    RenderPanel(PanelKind::Palette, "editor-watch-palette");
    RenderPanel(PanelKind::Properties, "editor-watch-properties");
    editor.OpenActions(ButtonTrigger::LeftClick);
    RenderPanel(PanelKind::Dialog, "editor-actions");
    const auto& elements = editor.watch.Document().elements;
    const auto clock = std::ranges::find_if(elements, [](const ModularElement& element) -> bool {
        const auto* label = std::get_if<LabelElement>(&element);
        return label != nullptr && std::holds_alternative<ClockLabelContent>(label->content);
    });
    ASSERT_NE(clock, elements.end());
    editor.watch.selected = static_cast<size_t>(clock - elements.begin());
    editor.CloseDialog();
    RenderPanel(PanelKind::Properties, "editor-clock-properties");

    editor.SwitchTarget(EditTarget::Keyboard);
    editor.keyboard.selected = 3;
    editor.Touch();
    RenderPanel(PanelKind::Preview, "editor-keyboard-preview");
    RenderPanel(PanelKind::Palette, "editor-keyboard-palette");
    RenderPanel(PanelKind::Properties, "editor-key-properties");
    editor.OpenKeyPicker();
    RenderPanel(PanelKind::Dialog, "editor-key-picker");
    editor.OpenColor({
        .title = "Key colour",
        .get = [](const Editor&) -> std::string { return "#8aadf4"; },
        .set = [](Editor&, const std::string&) -> void {},
        .hue = 0.6f,
    });
    RenderPanel(PanelKind::Dialog, "editor-color");
    editor.keyboard.selected.reset();
    editor.Touch();
    RenderPanel(PanelKind::Properties, "editor-keyboard-properties");
}
