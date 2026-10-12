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

#include "CommandBatch.hpp"
#include "CommandBuffer.hpp"
#include "Config.hpp"
#include "Device.hpp"
#include "Editor.hpp"
#include "EditorPanel.hpp"
#include "GpuReadback.hpp"
#include "Input.hpp"
#include "Keyboard.hpp"
#include "KeyboardProfiles.hpp"
#include "KeyControl.hpp"
#include "Keymap.hpp"
#include "ModifierControl.hpp"
#include "OverlayContainer.hpp"
#include "OverlayLayout.hpp"
#include "Png.hpp"
#include "ScopedRecordingHid.hpp"
#include "State.hpp"
#include "Task.hpp"
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
        RemoveDrafts();
        *g_input = InputState();
        g_config->grab_smoothing = false;
        g_config->pointer_smoothing = false;
        g_config->keyboard_profile = "keychron-v1-ansi";
        g_input->SetHmd(Eigen::Affine3f::Identity());
        m_overlays = OverlayContainer::CreateEmpty();
        auto watch = Watch::Create();
        ASSERT_TRUE(watch.has_value());
        m_overlays->Add(std::move(*watch));
        auto keyboard = Keyboard::Create(nullptr);
        ASSERT_TRUE(keyboard.has_value());
        m_overlays->Add(std::move(*keyboard));
        AimAway(0);
        AimAway(1);
    }

    auto TearDown() -> void override {
        if (!m_overlays) {
            return;
        }
        g_input->SetFocusedTextInput(nullptr);
        m_overlays.reset();
        RemoveDrafts();
        if (ScratchConfig()) {
            std::error_code error;
            std::filesystem::remove(Watch::UserFile("default"), error);
            std::filesystem::remove_all(ConfigStore::Root() / "watches", error);
            std::filesystem::remove_all(Theme::UserDirectory(), error);
            g_themes->Reload();
            g_themes->SetActive(g_themes->Find("macchiato"));
            g_config->theme = "macchiato";
            g_config->watch_profile = "default";
            std::filesystem::remove(KeyboardProfiles::UserDirectory() / "keychron-v1-ansi-custom.yaml", error);
        }
        g_keyboard_profiles->Reload();
        g_config->keyboard_profile = "keychron-v1-ansi";
        std::deque<Task> drained;
        g_tasks->TakeDue(drained);
    }

    static auto ScratchConfig() -> bool { return std::getenv("XDG_CONFIG_HOME") != nullptr; }

    static auto RemoveDrafts() -> void {
        std::error_code error;
        std::filesystem::remove(Editor::DraftFile(EditTarget::Watch), error);
        std::filesystem::remove(Editor::DraftFile(EditTarget::Keyboard), error);
    }

    auto Recreate() -> void {
        m_overlays = OverlayContainer::CreateEmpty();
        auto watch = Watch::Create();
        ASSERT_TRUE(watch.has_value());
        m_overlays->Add(std::move(*watch));
        auto keyboard = Keyboard::Create(nullptr);
        ASSERT_TRUE(keyboard.has_value());
        m_overlays->Add(std::move(*keyboard));
    }

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
                        } else if (std::holds_alternative<OpenEditorTask>(value)) {
                            m_overlays->OpenEditor();
                        }
                    },
                    task
                );
            }
        }
    }

    auto Step() -> void {
        m_overlays->UpdateEditor();
        RunTasks();
        for (const auto kind : Editor::kPanels) {
            if (auto* panel = Panel(kind)) {
                EXPECT_TRUE(panel->Backend()->ShouldRender().has_value());
            }
        }
    }

    auto Open() -> Editor& {
        m_overlays->OpenEditor();
        RunTasks();
        Step();
        Step();
        auto& editor = *m_overlays->ActiveEditor();
        editor.WatchDesign().SetHold({});
        editor.KeyboardDesign().SetHold({});
        return editor;
    }

    auto Frame(const PointerState& left, const PointerState& right = {}) -> void {
        g_input->PreUpdate();
        g_input->SetDeltaSeconds(kFrameSeconds);
        g_input->Pointers()[0].SetNow(left);
        g_input->Pointers()[1].SetNow(right);
        g_input->PostUpdate();
        Step();
        InputState::Interact();
        RunTasks();
    }

    auto Panel(PanelKind kind) -> Overlay* { return m_overlays->ByName(Editor::PanelName(kind)); }

    auto AimAt(size_t pointer, PanelKind kind, const Eigen::Vector2f& uv) -> void {
        auto* panel = Panel(kind);
        ASSERT_NE(panel, nullptr);
        const auto corners = OverlayLayout::Corners(*panel);
        ASSERT_TRUE(corners.has_value());
        const auto& c = *corners;
        const Eigen::Vector3f point = c[0] + (c[1] - c[0]) * uv.x() + (c[3] - c[0]) * (1.0f - uv.y());
        const Eigen::Vector3f normal = (c[1] - c[0]).cross(c[3] - c[0]).normalized();
        const Eigen::Matrix3f linear = panel->State().Transform().linear();
        Eigen::Affine3f pose = Eigen::Affine3f::Identity();
        pose.linear() = linear / linear.col(0).norm();
        pose.translation() = point + normal * 0.4f;
        g_input->Pointers()[pointer].SetPose(pose);
    }

    auto AimAway(size_t pointer) -> void {
        g_input->Pointers()[pointer].SetPose(
            Eigen::Affine3f(Eigen::AngleAxisf(std::numbers::pi_v<float>, Eigen::Vector3f::UnitY()))
        );
    }

    auto RenderPanel(PanelKind kind, const std::string& name) -> void {
        auto* panel = Panel(kind);
        ASSERT_NE(panel, nullptr);
        auto& backend = *panel->Backend();
        ASSERT_TRUE(backend.ShouldRender().has_value());
        const auto meta = backend.Meta();
        ASSERT_TRUE(meta.has_value());
        auto target = g_device->CreateRenderTexture(
            meta->extent.width,
            meta->extent.height,
            g_device->NativeFormat(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        );
        ASSERT_TRUE(target.has_value());
        CommandBatch batch;
        const auto rendered = backend.Render(*target, batch, 1.0f);
        ASSERT_TRUE(rendered.has_value() && *rendered);
        ASSERT_TRUE(batch.Submit().has_value());
        g_device->GraphicsQueue().WaitIdle();
        const auto pixels = GpuReadback::Read(**target);
        EXPECT_GT(GpuReadback::DistinctColors(pixels), 2U);
        Png::Write(TestApp::OutputDir() / std::format("{}.png", name), meta->extent.width, meta->extent.height, pixels);
    }

    auto LiveWatchControls() -> size_t {
        auto* watch = m_overlays->ByName(kWatchName);
        auto* canvas = dynamic_cast<Canvas*>(watch->Backend());
        return canvas != nullptr ? canvas->ControlCount() : 0;
    }

    std::unique_ptr<OverlayContainer> m_overlays;
};

} // namespace

TEST_F(EditorWorkspace, TheWatchButtonOpensAWorkspaceAroundYou) {
    ActionRunner runner;
    runner.Run(SystemButtonAction{.action = SystemAction::Edit});
    RunTasks();
    ASSERT_NE(m_overlays->ActiveEditor(), nullptr);
    Step();

    for (const auto kind : Editor::kPanels) {
        ASSERT_NE(Panel(kind), nullptr) << Editor::PanelName(kind);
    }
    const Eigen::Vector3f preview = Panel(PanelKind::Preview)->State().Transform().translation();
    EXPECT_NEAR(preview.x(), 0.0f, 1e-4f);
    EXPECT_LT(preview.z(), -0.5f);
    EXPECT_GT(Panel(PanelKind::Toolbar)->State().Transform().translation().y(), preview.y());
    EXPECT_LT(Panel(PanelKind::Palette)->State().Transform().translation().x(), -0.2f);
    EXPECT_GT(Panel(PanelKind::Properties)->State().Transform().translation().x(), 0.2f);
    EXPECT_TRUE(Panel(PanelKind::Preview)->State().WantVisible());
    EXPECT_FALSE(Panel(PanelKind::Dialog)->State().WantVisible());
    EXPECT_FALSE(Panel(PanelKind::Ghost)->State().WantVisible());
    EXPECT_EQ(Panel(PanelKind::Preview)->State().AttachedTo(), RelativeTo::Stage);
}

TEST_F(EditorWorkspace, TheWorkspaceOpensWhereverYouLook) {
    g_input->SetHmd(
        Eigen::Translation3f(1.0f, 1.6f, 0.0f) * Eigen::AngleAxisf(std::numbers::pi_v<float> / 2.0f, Eigen::Vector3f::UnitY())
    );
    Open();
    const Eigen::Vector3f preview = Panel(PanelKind::Preview)->State().Transform().translation();
    EXPECT_LT(preview.x(), 0.5f);
    EXPECT_NEAR(preview.z(), 0.0f, 1e-3f);
    EXPECT_NEAR(preview.y(), 1.6f - 0.08f, 1e-3f);
}

TEST_F(EditorWorkspace, GrabbingAnyPanelCarriesTheWholeWorkspace) {
    Open();
    const Eigen::Vector3f before = Panel(PanelKind::Preview)->State().Transform().translation();
    AimAt(1, PanelKind::Toolbar, {0.5f, 0.5f});
    Frame({}, {.grab = true});
    ASSERT_TRUE(g_input->Pointers()[1].Interaction().grabbed.has_value());
    g_input->Pointers()[1].Pose().translation() += Eigen::Vector3f(0.2f, 0.0f, 0.0f);
    Frame({}, {.grab = true});
    Frame({}, {.grab = true});
    const Eigen::Vector3f after = Panel(PanelKind::Preview)->State().Transform().translation();
    EXPECT_TRUE(after.isApprox(before + Eigen::Vector3f(0.2f, 0.0f, 0.0f), 1e-3f)) << after.transpose();
}

TEST_F(EditorWorkspace, DraggingFromThePaletteDropsOntoThePreview) {
    auto& editor = Open();
    const auto elements = editor.WatchDesign().Document().Elements().size();
    const auto controls = LiveWatchControls();

    AimAt(1, PanelKind::Palette, {0.5f, 121.0f / 420.0f});
    Frame({});
    Frame({}, {.click = true});
    ASSERT_TRUE(editor.Carrying().has_value());
    EXPECT_TRUE(Panel(PanelKind::Ghost)->State().WantVisible() || (Step(), Panel(PanelKind::Ghost)->State().WantVisible()));

    AimAt(1, PanelKind::Preview, {0.5f, 0.4f});
    Frame({}, {.click = true});
    Frame({});
    EXPECT_FALSE(editor.Carrying().has_value());
    ASSERT_EQ(editor.WatchDesign().Document().Elements().size(), elements + 1);
    EXPECT_TRUE(std::holds_alternative<ButtonElement>(editor.WatchDesign().Document().Elements().back()));
    EXPECT_EQ(editor.WatchDesign().Selected(), elements);
    Step();
    EXPECT_GT(LiveWatchControls(), controls);
    EXPECT_TRUE(editor.Dirty());

    const auto lines = m_overlays->EditorLines();
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_GT(lines.front().length, 0.05f);
}

TEST_F(EditorWorkspace, ClickingAPaletteItemAddsItAndReleasingInTheAirCancels) {
    auto& editor = Open();
    const auto elements = editor.WatchDesign().Document().Elements().size();
    AimAt(1, PanelKind::Palette, {0.5f, 121.0f / 420.0f});
    Frame({});
    Frame({}, {.click = true});
    Frame({});
    EXPECT_EQ(editor.WatchDesign().Document().Elements().size(), elements + 1);

    Frame({}, {.click = true});
    ASSERT_TRUE(editor.Carrying().has_value());
    AimAway(1);
    Frame({}, {.click = true});
    Frame({});
    EXPECT_FALSE(editor.Carrying().has_value());
    EXPECT_EQ(editor.WatchDesign().Document().Elements().size(), elements + 1);
}

TEST_F(EditorWorkspace, ThePreviewMovesElementsWithThePointer) {
    auto& editor = Open();
    editor.WatchDesign().SetSnap(false);
    const auto& config = editor.WatchDesign().Document();
    const auto index = config.Elements().size() - 1;
    const auto rect = editor.WatchDesign().VisualRect(index);
    const Eigen::Vector2f size = editor.PanelSize(PanelKind::Preview);
    const auto uv = [&](float x, float y) -> Eigen::Vector2f { return editor.WatchDesign().ToPreview({x, y}).cwiseQuotient(size); };

    AimAt(1, PanelKind::Preview, uv(rect.x + rect.w / 2.0f, rect.y + rect.h / 2.0f));
    Frame({});
    Frame({}, {.click = true});
    EXPECT_EQ(editor.WatchDesign().Selected(), index);
    AimAt(1, PanelKind::Preview, uv(rect.x + rect.w / 2.0f - 30.0f, rect.y + rect.h / 2.0f));
    Frame({}, {.click = true});
    Frame({});
    EXPECT_NEAR(editor.WatchDesign().VisualRect(index).x, rect.x - 30.0f, 0.5f);
    EXPECT_TRUE(editor.WatchDesign().Revisions().CanUndo());
}

TEST_F(EditorWorkspace, TypingOnTheKeyboardFillsTheFocusedField) {
    auto& editor = Open();
    auto us = Keymap::FromNames("us", "");
    ASSERT_TRUE(us.has_value());
    auto state = std::make_shared<KeyboardState>();
    state->SetTypingKeymap(std::make_shared<const Keymap>(std::move(*us)));
    KeyControl h(state, VirtualKey::H);
    KeyControl i(state, VirtualKey::I);
    KeyControl backspace(state, VirtualKey::BackSpace);
    KeyControl enter(state, VirtualKey::Return);
    ModifierControl shift(state, kModShift);
    const auto tap = [](Control& control) -> void {
        control.OnPress(PointerMode::Left);
        control.OnRelease();
    };

    std::string committed;
    editor.BeginText("test", "", [&committed](Editor&, const std::string& text) -> void { committed = text; });
    ScopedRecordingHid hid;
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
    EXPECT_EQ(g_input->FocusedTextInput(), nullptr);
    EXPECT_TRUE(hid.Events().empty());

    tap(h);
    EXPECT_EQ(hid.Events().size(), 2U);
}

TEST_F(EditorWorkspace, PressingAnotherPanelKeepsWhatWasTyped) {
    auto& editor = Open();
    const auto index = editor.WatchDesign().Document().Elements().size() - 1;
    editor.WatchDesign().SetSelected(index);
    editor.BeginText("button.text", "", [index](Editor& owner, const std::string& text) -> void {
        owner.EditWatch([index, &text](ModularUiConfig& config) -> void {
            std::get<ButtonElement>(config.Elements().at(index)).text = text;
        });
    });
    editor.OnKey(VirtualKey::O, "O");
    editor.OnKey(VirtualKey::K, "k");
    AimAt(1, PanelKind::Toolbar, {0.5f, 0.5f});
    Frame({});
    Frame({}, {.click = true});
    EXPECT_FALSE(editor.Focused().has_value());
    EXPECT_EQ(std::get<ButtonElement>(editor.WatchDesign().Document().Elements().at(index)).text, "Ok");
    Frame({});
}

TEST_F(EditorWorkspace, PressingAKeyAssignsIt) {
    auto& editor = Open();
    editor.SwitchTarget(EditTarget::Keyboard);
    editor.KeyboardDesign().AddKey(0.0f, editor.KeyboardDesign().Document().Rows() - 1);
    editor.KeyboardDesign().Selected().reset();
    editor.KeyboardDesign().Revisions().Edit([](KeyboardLayout& layout) -> void { layout.SetRows(layout.Rows() + 1); });
    editor.KeyboardDesign().AddKey(0.0f, editor.KeyboardDesign().Document().Rows() - 1);
    const auto index = *editor.KeyboardDesign().Selected();

    auto state = std::make_shared<KeyboardState>();
    KeyControl z(state, VirtualKey::Z);
    editor.BeginCapture(false, [index](Editor& owner, const std::vector<VirtualKey>& keys) -> void {
        owner.EditKeyboard([index, &keys](KeyboardLayout& layout) -> void {
            layout.Keys().at(index).action = TypeKey{.key = keys.back()};
        });
    });
    EXPECT_TRUE(g_input->FocusedTextInput()->WantsRawKeys());
    z.OnPress(PointerMode::Left);
    z.OnRelease();
    const auto* typed = std::get_if<TypeKey>(&editor.KeyboardDesign().Document().Keys().at(index).action);
    ASSERT_NE(typed, nullptr);
    EXPECT_EQ(typed->key, VirtualKey::Z);
    EXPECT_FALSE(editor.Capturing());
}

TEST_F(EditorWorkspace, SaveWritesTheWatchAndRevertRemovesIt) {
    if (!ScratchConfig()) {
        GTEST_SKIP() << "writes config files, set XDG_CONFIG_HOME to a scratch directory";
    }
    auto& editor = Open();
    const float width = editor.WatchDesign().Document().Width();
    editor.EditWatch([](ModularUiConfig& config) -> void { config.SetWidth(config.Width() + 0.01f); });
    editor.Save();
    ASSERT_TRUE(std::filesystem::exists(Watch::UserFile("default")));
    EXPECT_NEAR(Watch::LoadConfig("default").Width(), width + 0.01f, 1e-5f);
    EXPECT_FALSE(editor.Dirty());

    editor.Revert();
    EXPECT_TRUE(std::filesystem::exists(Watch::UserFile("default")));
    editor.Revert();
    EXPECT_FALSE(std::filesystem::exists(Watch::UserFile("default")));
    EXPECT_NEAR(editor.WatchDesign().Document().Width(), width, 1e-5f);
}

TEST_F(EditorWorkspace, SavingAKeyboardAddsAProfileAndSwitchesToIt) {
    if (!ScratchConfig()) {
        GTEST_SKIP() << "writes config files, set XDG_CONFIG_HOME to a scratch directory";
    }
    auto& editor = Open();
    editor.SwitchTarget(EditTarget::Keyboard);
    editor.KeyboardDesign().AddColumn();
    editor.SaveAsNew();
    EXPECT_EQ(editor.KeyboardDesign().Document().Name(), "keychron-v1-ansi-custom");
    EXPECT_TRUE(std::filesystem::exists(KeyboardProfiles::UserDirectory() / "keychron-v1-ansi-custom.yaml"));
    const auto& saved = g_keyboard_profiles->Find("keychron-v1-ansi-custom");
    EXPECT_EQ(saved.Name(), "keychron-v1-ansi-custom");
    EXPECT_FLOAT_EQ(saved.Columns(), 17.0f);
    EXPECT_EQ(g_config->keyboard_profile, "keychron-v1-ansi-custom");
    RunTasks();
    const auto meta = m_overlays->ByName(kKeyboardName)->Backend()->Meta();
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->extent.width, (17U * 80U) + 28U);
    EXPECT_FALSE(editor.Dirty());
}

TEST_F(EditorWorkspace, DoneAsksBeforeDroppingUnsavedWork) {
    auto& editor = Open();
    const auto controls = LiveWatchControls();
    editor.WatchDesign().Insert(FormPart::Panel, std::nullopt);
    editor.EditWatch([](ModularUiConfig&) -> void {});
    Step();
    EXPECT_GT(LiveWatchControls(), controls);

    editor.Done();
    EXPECT_FALSE(editor.Closing());
    editor.Discard();
    EXPECT_TRUE(editor.Closing());
    Step();
    EXPECT_EQ(m_overlays->ActiveEditor(), nullptr);
    for (const auto kind : Editor::kPanels) {
        EXPECT_EQ(Panel(kind), nullptr);
    }
    EXPECT_EQ(LiveWatchControls(), controls);
}

TEST_F(EditorWorkspace, LayersSpreadInDepthAndLetYouReachThrough) {
    auto& editor = Open();
    editor.SetExploded(true);
    Step();
    EXPECT_FALSE(Panel(PanelKind::Preview)->State().WantVisible());
    const float back = Panel(PanelKind::BackLayer)->State().Transform().translation().z();
    const float front = Panel(PanelKind::FrontLayer)->State().Transform().translation().z();
    EXPECT_TRUE(Panel(PanelKind::MiddleLayer)->State().WantVisible());
    EXPECT_NEAR(front - back, 2.0f * Editor::kLayerSpacing, 1e-4f);

    const Eigen::Vector2f size = editor.PanelSize(PanelKind::Preview);
    const auto uv = [&](float x, float y) -> Eigen::Vector2f { return editor.WatchDesign().ToPreview({x, y}).cwiseQuotient(size); };
    AimAt(1, PanelKind::FrontLayer, uv(150.0f, 150.0f));
    Frame({});
    Frame({}, {.click = true});
    Frame({});
    ASSERT_TRUE(editor.WatchDesign().Selected().has_value());
    EXPECT_TRUE(
        std::holds_alternative<PanelElement>(editor.WatchDesign().Document().Elements().at(*editor.WatchDesign().Selected()))
    );
    EXPECT_EQ(g_input->Pointers()[1].Interaction().hovered_id, Panel(PanelKind::BackLayer)->State().Id());

    AimAt(1, PanelKind::FrontLayer, uv(350.0f, 68.0f));
    Frame({});
    Frame({}, {.click = true});
    Frame({});
    ASSERT_TRUE(editor.WatchDesign().Selected().has_value());
    EXPECT_TRUE(
        std::holds_alternative<ButtonElement>(editor.WatchDesign().Document().Elements().at(*editor.WatchDesign().Selected()))
    );
    EXPECT_EQ(g_input->Pointers()[1].Interaction().hovered_id, Panel(PanelKind::FrontLayer)->State().Id());

    RenderPanel(PanelKind::BackLayer, "editor-layer-back");
    RenderPanel(PanelKind::MiddleLayer, "editor-layer-middle");
    RenderPanel(PanelKind::FrontLayer, "editor-layer-front");
}

TEST_F(EditorWorkspace, OnlyOneHandUsesTheEditorAtATime) {
    auto& editor = Open();
    editor.WatchDesign().SetSnap(false);
    const auto index = editor.WatchDesign().Document().Elements().size() - 1;
    const auto rect = editor.WatchDesign().VisualRect(index);
    const Eigen::Vector2f size = editor.PanelSize(PanelKind::Preview);
    const auto uv = [&](float x, float y) -> Eigen::Vector2f { return editor.WatchDesign().ToPreview({x, y}).cwiseQuotient(size); };

    AimAt(1, PanelKind::Preview, uv(rect.x + rect.w / 2.0f, rect.y + rect.h / 2.0f));
    AimAt(0, PanelKind::Preview, uv(60.0f, 60.0f));
    Frame({});
    Frame({}, {.click = true});
    EXPECT_EQ(editor.Owner(), 1U);
    EXPECT_EQ(editor.WatchDesign().Selected(), index);

    Frame({.click = true}, {.click = true});
    AimAt(0, PanelKind::Preview, uv(20.0f, 20.0f));
    Frame({.click = true}, {.click = true});
    EXPECT_EQ(editor.WatchDesign().Selected(), index);
    EXPECT_NEAR(editor.WatchDesign().VisualRect(index).x, rect.x, 0.5f);

    AimAt(1, PanelKind::Preview, uv(rect.x + rect.w / 2.0f - 20.0f, rect.y + rect.h / 2.0f));
    Frame({.click = true}, {.click = true});
    Frame({.click = true});
    EXPECT_NEAR(editor.WatchDesign().VisualRect(index).x, rect.x - 20.0f, 0.5f);
    EXPECT_FALSE(editor.Owner().has_value());
    Frame({});
}

TEST_F(EditorWorkspace, RecenterAndShowHideIncludeTheWorkspace) {
    Open();
    g_input->SetHmd(Eigen::Affine3f(Eigen::AngleAxisf(std::numbers::pi_v<float> / 2.0f, Eigen::Vector3f::UnitY())));
    m_overlays->Recenter();
    Step();
    EXPECT_LT(Panel(PanelKind::Preview)->State().Transform().translation().x(), -0.5f);

    m_overlays->ShowHide();
    Step();
    EXPECT_FALSE(Panel(PanelKind::Preview)->State().WantVisible());
    EXPECT_FALSE(Panel(PanelKind::Toolbar)->State().WantVisible());
    m_overlays->ShowHide();
    Step();
    EXPECT_TRUE(Panel(PanelKind::Preview)->State().WantVisible());
    EXPECT_TRUE(Panel(PanelKind::Toolbar)->State().WantVisible());
}

TEST_F(EditorWorkspace, TheColourWheelPicksLiveAndUndoesInOneStep) {
    auto& editor = Open();
    const auto index = editor.WatchDesign().Document().Elements().size() - 1;
    const auto color = [&editor, index]() -> std::string {
        return std::get<ButtonElement>(editor.WatchDesign().Document().Elements().at(index)).bg_color;
    };
    const auto before = color();
    editor.OpenColor({
        .title = "Colour",
        .get = [index](const Editor& owner) -> std::string {
            return std::get<ButtonElement>(owner.WatchDesign().Document().Elements().at(index)).bg_color;
        },
        .set = [index](Editor& owner, const std::string& value) -> void {
            owner.EditWatch([index, &value](ModularUiConfig& config) -> void {
                std::get<ButtonElement>(config.Elements().at(index)).bg_color = value;
            });
        },
        .value = 1.0f,
    });
    Step();
    const auto& wheel = editor.WheelRect();
    ASSERT_GT(wheel.w, 0.0f);
    const Eigen::Vector2f center(wheel.x + wheel.w / 2.0f, wheel.y + wheel.h / 2.0f);
    EXPECT_TRUE(editor.DialogPointer(center + Eigen::Vector2f(wheel.w / 2.0f, 0.0f), true));
    EXPECT_EQ(color(), "#ff0000");
    EXPECT_TRUE(editor.DialogHover(center));
    EXPECT_EQ(color(), "#ffffff");
    EXPECT_TRUE(editor.DialogPointer(center, false));
    RenderPanel(PanelKind::Dialog, "editor-color-wheel-bright");
    const auto& bar = editor.BrightnessRect();
    EXPECT_TRUE(editor.DialogPointer({bar.x, bar.y + bar.h / 2.0f}, true));
    EXPECT_TRUE(editor.DialogPointer({bar.x, bar.y + bar.h / 2.0f}, false));
    EXPECT_EQ(color(), "#000000");
    EXPECT_FALSE(editor.DialogPointer({1.0f, 1.0f}, true));
    RenderPanel(PanelKind::Dialog, "editor-color-wheel");

    editor.Undo();
    EXPECT_EQ(color(), "#ffffff");
    editor.Undo();
    EXPECT_EQ(color(), before);
}

TEST_F(EditorWorkspace, ExtraFormsGrowTheLiveWatch) {
    auto& editor = Open();
    const auto width = m_overlays->ByName(kWatchName)->Backend()->Meta()->extent.width;
    editor.WatchDesign().Insert(FormPart::Form, std::nullopt);
    editor.EditWatch([](ModularUiConfig&) -> void {});
    Step();
    EXPECT_GT(m_overlays->ByName(kWatchName)->Backend()->Meta()->extent.width, width);
    RenderPanel(PanelKind::Preview, "editor-extra-form");
    RenderPanel(PanelKind::Properties, "editor-extra-form-properties");
}

TEST_F(EditorWorkspace, TimezonesAreSearchedAndPicked) {
    auto& editor = Open();
    const auto& elements = editor.WatchDesign().Document().Elements();
    const auto clock = std::ranges::find_if(elements, [](const ModularElement& element) -> bool {
        const auto* label = std::get_if<LabelElement>(&element);
        return label != nullptr && std::holds_alternative<ClockLabelContent>(label->content);
    });
    ASSERT_NE(clock, elements.end());
    const auto index = static_cast<size_t>(clock - elements.begin());
    editor.WatchDesign().SetSelected(index);
    editor.OpenTimezones(index);
    editor.BeginText("timezone.search", "", [](Editor&, const std::string&) -> void {});
    for (const auto* letter : {"h", "e", "l", "s"}) {
        editor.OnKey(VirtualKey::A, letter);
    }
    Step();
    RenderPanel(PanelKind::Dialog, "editor-timezones");

    auto* dialog = dynamic_cast<EditorPanel*>(Panel(PanelKind::Dialog)->Backend());
    ASSERT_NE(dialog, nullptr);
    auto* canvas = dialog->CanvasForTest();
    ASSERT_NE(canvas, nullptr);
    Control* helsinki = nullptr;
    size_t results = 0;
    for (size_t i = 0; i < canvas->ControlCount(); ++i) {
        auto& control = canvas->ControlAt(i);
        if (control.Text().starts_with("Europe/") || control.Text().starts_with("Asia/")
            || control.Text().starts_with("America/")) {
            ++results;
        }
        if (control.Text() == "Europe/Helsinki") {
            helsinki = &control;
        }
    }
    ASSERT_NE(helsinki, nullptr);
    EXPECT_LE(results, 3U);
    helsinki->OnRelease();
    const auto& content = std::get<LabelElement>(editor.WatchDesign().Document().Elements().at(index)).content;
    const auto& zone = std::get<ClockLabelContent>(content).timezone;
    ASSERT_TRUE(zone.has_value());
    EXPECT_EQ(std::get<std::string>(*zone), "Europe/Helsinki");
    EXPECT_EQ(editor.Dialog(), DialogKind::None);
    EXPECT_EQ(g_input->FocusedTextInput(), nullptr);
}

TEST_F(EditorWorkspace, TheEditButtonClosesTheEditorAndKeepsADraft) {
    auto& editor = Open();
    const auto controls = LiveWatchControls();
    const auto elements = editor.WatchDesign().Document().Elements().size();
    editor.WatchDesign().Insert(FormPart::Panel, std::nullopt);
    editor.EditWatch([](ModularUiConfig&) -> void {});
    Step();
    EXPECT_GT(LiveWatchControls(), controls);

    m_overlays->OpenEditor();
    Step();
    EXPECT_EQ(m_overlays->ActiveEditor(), nullptr);
    EXPECT_TRUE(std::filesystem::exists(Editor::DraftFile(EditTarget::Watch)));
    EXPECT_EQ(LiveWatchControls(), controls);

    auto& reopened = Open();
    EXPECT_EQ(reopened.WatchDesign().Document().Elements().size(), elements + 1);
    EXPECT_TRUE(reopened.Dirty());
    EXPECT_GT(LiveWatchControls(), controls);
    reopened.Undo();
    EXPECT_FALSE(reopened.Dirty());
    reopened.Redo();
    reopened.Save();
    EXPECT_FALSE(std::filesystem::exists(Editor::DraftFile(EditTarget::Watch)));
}

TEST_F(EditorWorkspace, DraftsSurviveACrash) {
    auto& editor = Open();
    editor.SwitchTarget(EditTarget::Keyboard);
    editor.KeyboardDesign().AddColumn();
    editor.WatchDesign().Insert(FormPart::Button, std::nullopt);
    editor.SaveDrafts(true);
    Recreate();

    auto& recovered = Open();
    EXPECT_FLOAT_EQ(recovered.KeyboardDesign().Document().Columns(), 17.0f);
    EXPECT_TRUE(recovered.KeyboardDesign().Revisions().Dirty());
    EXPECT_TRUE(recovered.WatchDesign().Revisions().Dirty());
    recovered.Discard();
    Step();
    EXPECT_FALSE(std::filesystem::exists(Editor::DraftFile(EditTarget::Watch)));
    EXPECT_FALSE(std::filesystem::exists(Editor::DraftFile(EditTarget::Keyboard)));
}

TEST_F(EditorWorkspace, CityClocksAreOneComponentMeasuredByWhatTheyShow) {
    auto& editor = Open();
    const auto& config = editor.WatchDesign().Document();
    size_t clocks = 0;
    for (size_t i = 0; i < config.Elements().size(); ++i) {
        const auto* label = std::get_if<LabelElement>(&config.Elements()[i]);
        const auto* clock = label != nullptr ? std::get_if<ClockLabelContent>(&label->content) : nullptr;
        if (clock == nullptr || !clock->city_size) {
            continue;
        }
        ++clocks;
        const auto rect = editor.WatchDesign().VisualRect(i);
        const auto& placed = ModularElements::Rect(config.Elements()[i]);
        EXPECT_LT(
            rect.y,
            editor.WatchDesign().Form(0).y + placed[1] - static_cast<float>(label->font_size)
                - static_cast<float>(*clock->city_size)
        );
        EXPECT_GT(rect.w, 20.0f);
        EXPECT_LT(rect.w, 110.0f);
        EXPECT_LE(rect.x + rect.w, editor.WatchDesign().Form(0).x + editor.WatchDesign().Form(0).w);
        EXPECT_EQ(editor.WatchDesign().HitTest({rect.x + 5.0f, rect.y + 3.0f}).index, i);
    }
    EXPECT_EQ(clocks, 2U);
    EXPECT_FALSE(std::ranges::any_of(config.Elements(), [](const ModularElement& element) -> bool {
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
    EXPECT_EQ(g_themes->Active().rounding, Rounding::Square);
    EXPECT_TRUE(editor.Dirty());

    editor.NewTheme();
    EXPECT_TRUE(editor.ThemeHistory().Dirty());
    editor.Save();
    editor.NewTheme();
    EXPECT_EQ(editor.ThemeHistory().Current().name, "my-theme");
    EXPECT_EQ(g_config->theme, "my-theme");
    EXPECT_TRUE(std::filesystem::exists(Theme::UserDirectory() / "my-theme.yaml"));
    EXPECT_EQ(Theme::Lookup(g_themes->Find("my-theme"), "accent"), std::optional<std::string>("#00ff00"));

    editor.SelectTheme("latte");
    EXPECT_EQ(Canvas::ParseColor("@background").value(), Canvas::ParseColor("#eff1f5").value());
    Step();
    RenderPanel(PanelKind::Preview, "editor-theme-latte");
    editor.SelectTheme("my-theme");
    editor.DeleteTheme();
    editor.DeleteTheme();
    EXPECT_FALSE(std::filesystem::exists(Theme::UserDirectory() / "my-theme.yaml"));
    EXPECT_EQ(editor.ThemeHistory().Current().name, "macchiato");
    editor.Revert();
    editor.Revert();
    EXPECT_EQ(Canvas::ParseColor("@accent").value(), Canvas::ParseColor("#c6a0f6").value());
}

TEST_F(EditorWorkspace, WatchLayoutsAreChosenCreatedAndDeleted) {
    auto& editor = Open();
    const auto elements = editor.WatchDesign().Document().Elements().size();
    editor.NewWatch();
    EXPECT_EQ(editor.WatchProfile(), "watch-2");
    EXPECT_EQ(g_config->watch_profile, "watch-2");
    editor.WatchDesign().Insert(FormPart::Panel, std::nullopt);
    editor.Save();
    EXPECT_EQ(Watch::LoadConfig("watch-2").Elements().size(), elements + 1);

    editor.SelectWatch("default");
    EXPECT_EQ(editor.WatchDesign().Document().Elements().size(), elements);
    editor.SelectWatch("watch-2");
    EXPECT_EQ(editor.WatchDesign().Document().Elements().size(), elements + 1);
    RenderPanel(PanelKind::Properties, "editor-watch-layouts");
    editor.RenameWatch("mine");
    EXPECT_EQ(Watch::Profiles(), (std::vector<std::string>{"default", "mine"}));
    editor.DeleteWatch();
    editor.DeleteWatch();
    EXPECT_EQ(Watch::Profiles(), std::vector<std::string>{"default"});
    EXPECT_EQ(editor.WatchProfile(), "default");
}

TEST_F(EditorWorkspace, KeyboardsArePickedFromTheList) {
    auto& editor = Open();
    editor.SwitchTarget(EditTarget::Keyboard);
    editor.SelectKeyboard("full-size");
    EXPECT_EQ(editor.KeyboardDesign().Document().Name(), "full-size");
    EXPECT_EQ(g_config->keyboard_profile, "full-size");
    editor.KeyboardDesign().AddColumn();
    editor.SelectKeyboard("keychron-v1-ansi");
    EXPECT_EQ(editor.KeyboardDesign().Document().Name(), "full-size");
    RenderPanel(PanelKind::Properties, "editor-keyboards");
}

TEST_F(EditorWorkspace, LongNamesAreShortenedAndTheEditorFollowsTheTheme) {
    auto& editor = Open();
    const std::string name = "a-really-long-watch-layout-name-that-never-fits";
    {
        std::filesystem::create_directories(ConfigStore::Root() / "watches");
        std::ofstream(Watch::UserFile(name)) << "width: 0.1\nform: {width: 100, height: 50}\nelements: []\n";
    }
    editor.Touch();
    Step();
    auto* properties = dynamic_cast<EditorPanel*>(Panel(PanelKind::Properties)->Backend());
    ASSERT_NE(properties, nullptr);
    auto* canvas = properties->CanvasForTest();
    ASSERT_NE(canvas, nullptr);
    bool shortened = false;
    for (size_t i = 0; i < canvas->ControlCount(); ++i) {
        const auto& text = canvas->ControlAt(i).Text();
        EXPECT_NE(text, name);
        shortened = shortened || (text.starts_with("a-really") && text.ends_with("..."));
    }
    EXPECT_TRUE(shortened);
    RenderPanel(PanelKind::Properties, "editor-long-names");

    editor.SwitchTarget(EditTarget::Theme);
    editor.SelectTheme("latte");
    Step();
    RenderPanel(PanelKind::Toolbar, "editor-toolbar-latte");
    RenderPanel(PanelKind::Properties, "editor-theme-latte-properties");
    editor.SelectTheme("macchiato");
}

TEST_F(EditorWorkspace, PanelsRender) {
    auto& editor = Open();
    editor.WatchDesign().SetSelected(editor.WatchDesign().Document().Elements().size() - 2);
    editor.Touch();
    RenderPanel(PanelKind::Preview, "editor-watch-preview");
    RenderPanel(PanelKind::Toolbar, "editor-toolbar");
    RenderPanel(PanelKind::Palette, "editor-watch-palette");
    RenderPanel(PanelKind::Properties, "editor-watch-properties");
    editor.OpenActions(ButtonTrigger::LeftClick);
    RenderPanel(PanelKind::Dialog, "editor-actions");
    const auto& elements = editor.WatchDesign().Document().Elements();
    const auto clock = std::ranges::find_if(elements, [](const ModularElement& element) -> bool {
        const auto* label = std::get_if<LabelElement>(&element);
        return label != nullptr && std::holds_alternative<ClockLabelContent>(label->content);
    });
    ASSERT_NE(clock, elements.end());
    editor.WatchDesign().SetSelected(static_cast<size_t>(clock - elements.begin()));
    editor.CloseDialog();
    RenderPanel(PanelKind::Properties, "editor-clock-properties");

    editor.SwitchTarget(EditTarget::Keyboard);
    editor.KeyboardDesign().SetSelected(3);
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
    editor.KeyboardDesign().Selected().reset();
    editor.Touch();
    RenderPanel(PanelKind::Properties, "editor-keyboard-properties");
}
