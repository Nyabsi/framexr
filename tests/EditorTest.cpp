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
#include "ConfigStore.hpp"
#include "FontCache.hpp"
#include "Forms.hpp"
#include "Device.hpp"
#include "ActionRunner.hpp"
#include "Editor.hpp"
#include "document/FormElements.hpp"
#include "GpuReadback.hpp"
#include "Interaction.hpp"
#include "Keyboard.hpp"
#include "KeyboardLayout.hpp"
#include "Keymap.hpp"
#include "Container.hpp"
#include "Png.hpp"
#include "ScopedRecordingHid.hpp"
#include "AppState.hpp"
#include "TaskQueue.hpp"
#include "TestApp.hpp"
#include "FormTheme.hpp"
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
        *g_interaction = Interaction();
        g_config->grab_smoothing = false;
        g_config->pointer_smoothing = false;
        g_config->keyboard_profile = "keychron-v1-ansi";
        g_interaction->SetHmd(Eigen::Affine3f::Identity());
        m_overlays = Container::CreateEmpty();
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
        g_interaction->SetFocusedTextInput(nullptr);
        m_overlays.reset();
        RemoveDrafts();
        if (ScratchConfig()) {
            std::error_code error;
            std::filesystem::remove(Forms::WatchFile("default"), error);
            std::filesystem::remove_all(ConfigStore::Root() / "watches", error);
            std::filesystem::remove_all(FormTheme::UserDirectory(), error);
            g_themes->Reload();
            g_themes->SetActive(g_themes->Find("macchiato"));
            g_config->theme = "macchiato";
            g_config->watch_profile = "default";
            std::filesystem::remove(KeyboardLayout::UserDirectory() / "keychron-v1-ansi-custom.yaml", error);
        }
        KeyboardLayout::Reload();
        g_config->keyboard_profile = "keychron-v1-ansi";
        std::deque<Task> drained;
        g_tasks->TakeDue(drained);
    }

    static auto ScratchConfig() -> bool { return std::getenv("XDG_CONFIG_HOME") != nullptr; }

    static auto RemoveDrafts() -> void {
        std::error_code error;
        std::filesystem::remove(Drafts::File(EditTarget::Watch), error);
        std::filesystem::remove(Drafts::File(EditTarget::Keyboard), error);
    }

    auto Recreate() -> void {
        m_overlays = Container::CreateEmpty();
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
        for (const auto kind : PanelPlacement::kPanels) {
            if (auto* panel = Panel(kind)) {
                panel->Update();
                EXPECT_TRUE(panel->Composer()->ShouldRender().has_value());
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
        g_interaction->PreUpdate();
        g_interaction->SetDeltaSeconds(kFrameSeconds);
        g_interaction->Pointers()[0].SetNow(left);
        g_interaction->Pointers()[1].SetNow(right);
        g_interaction->PostUpdate();
        Step();
        Interaction::Interact();
        RunTasks();
    }

    auto Panel(PanelKind kind) -> View* { return m_overlays->ByName(PanelPlacement::PanelName(kind)); }

    auto AimAt(size_t pointer, PanelKind kind, const Eigen::Vector2f& uv) -> void {
        auto* panel = Panel(kind);
        ASSERT_NE(panel, nullptr);
        const auto corners = Container::Corners(*panel);
        ASSERT_TRUE(corners.has_value());
        const auto& c = *corners;
        const Eigen::Vector3f point = c[0] + (c[1] - c[0]) * uv.x() + (c[3] - c[0]) * (1.0f - uv.y());
        const Eigen::Vector3f normal = (c[1] - c[0]).cross(c[3] - c[0]).normalized();
        const Eigen::Matrix3f linear = panel->Transform().linear();
        Eigen::Affine3f pose = Eigen::Affine3f::Identity();
        pose.linear() = linear / linear.col(0).norm();
        pose.translation() = point + normal * 0.4f;
        g_interaction->Pointers()[pointer].SetPose(pose);
    }

    auto AimAway(size_t pointer) -> void {
        g_interaction->Pointers()[pointer].SetPose(
            Eigen::Affine3f(Eigen::AngleAxisf(std::numbers::pi_v<float>, Eigen::Vector3f::UnitY()))
        );
    }

    auto RenderPanel(PanelKind kind, const std::string& name) -> void {
        auto* panel = Panel(kind);
        ASSERT_NE(panel, nullptr);
        panel->Update();
        auto& backend = *panel->Composer();
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
        auto* canvas = watch->Composer();
        return canvas != nullptr ? canvas->Items().size() : 0;
    }

    std::unique_ptr<Container> m_overlays;
};

} // namespace

TEST_F(EditorWorkspace, TheWatchButtonOpensAWorkspaceAroundYou) {
    ActionRunner runner;
    runner.Run(SystemButtonAction{.action = SystemAction::Edit});
    RunTasks();
    ASSERT_NE(m_overlays->ActiveEditor(), nullptr);
    Step();

    for (const auto kind : PanelPlacement::kPanels) {
        ASSERT_NE(Panel(kind), nullptr) << PanelPlacement::PanelName(kind);
    }
    const Eigen::Vector3f preview = Panel(PanelKind::Preview)->Transform().translation();
    EXPECT_NEAR(preview.x(), 0.0f, 1e-4f);
    EXPECT_LT(preview.z(), -0.5f);
    EXPECT_GT(Panel(PanelKind::Toolbar)->Transform().translation().y(), preview.y());
    EXPECT_LT(Panel(PanelKind::Palette)->Transform().translation().x(), -0.2f);
    EXPECT_GT(Panel(PanelKind::Properties)->Transform().translation().x(), 0.2f);
    EXPECT_TRUE(Panel(PanelKind::Preview)->WantVisible());
    EXPECT_FALSE(Panel(PanelKind::Dialog)->WantVisible());
    EXPECT_FALSE(Panel(PanelKind::Ghost)->WantVisible());
    EXPECT_EQ(Panel(PanelKind::Preview)->AttachedTo(), RelativeTo::Stage);
}

TEST_F(EditorWorkspace, TheWorkspaceOpensWhereverYouLook) {
    g_interaction->SetHmd(
        Eigen::Translation3f(1.0f, 1.6f, 0.0f) * Eigen::AngleAxisf(std::numbers::pi_v<float> / 2.0f, Eigen::Vector3f::UnitY())
    );
    Open();
    const Eigen::Vector3f preview = Panel(PanelKind::Preview)->Transform().translation();
    EXPECT_LT(preview.x(), 0.5f);
    EXPECT_NEAR(preview.z(), 0.0f, 1e-3f);
    EXPECT_NEAR(preview.y(), 1.6f - 0.08f, 1e-3f);
}

TEST_F(EditorWorkspace, GrabbingAnyPanelCarriesTheWholeWorkspace) {
    Open();
    const Eigen::Vector3f before = Panel(PanelKind::Preview)->Transform().translation();
    AimAt(1, PanelKind::Toolbar, {0.5f, 0.5f});
    Frame({}, {.grab = true});
    ASSERT_TRUE(g_interaction->Pointers()[1].Interaction().grabbed.has_value());
    g_interaction->Pointers()[1].Pose().translation() += Eigen::Vector3f(0.2f, 0.0f, 0.0f);
    Frame({}, {.grab = true});
    Frame({}, {.grab = true});
    const Eigen::Vector3f after = Panel(PanelKind::Preview)->Transform().translation();
    EXPECT_TRUE(after.isApprox(before + Eigen::Vector3f(0.2f, 0.0f, 0.0f), 1e-3f)) << after.transpose();
}

TEST_F(EditorWorkspace, DraggingFromThePaletteDropsOntoThePreview) {
    auto& editor = Open();
    const auto elements = editor.WatchDesign().Document().Components().size();
    const auto controls = LiveWatchControls();

    AimAt(1, PanelKind::Palette, {0.5f, 121.0f / 420.0f});
    Frame({});
    Frame({}, {.click = true});
    ASSERT_TRUE(editor.Carrying().has_value());
    EXPECT_TRUE(Panel(PanelKind::Ghost)->WantVisible() || (Step(), Panel(PanelKind::Ghost)->WantVisible()));

    AimAt(1, PanelKind::Preview, {0.5f, 0.4f});
    Frame({}, {.click = true});
    Frame({});
    EXPECT_FALSE(editor.Carrying().has_value());
    ASSERT_EQ(editor.WatchDesign().Document().Components().size(), elements + 1);
    EXPECT_TRUE(std::holds_alternative<ButtonElement>(editor.WatchDesign().Document().Components().back()));
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
    const auto elements = editor.WatchDesign().Document().Components().size();
    AimAt(1, PanelKind::Palette, {0.5f, 121.0f / 420.0f});
    Frame({});
    Frame({}, {.click = true});
    Frame({});
    EXPECT_EQ(editor.WatchDesign().Document().Components().size(), elements + 1);

    Frame({}, {.click = true});
    ASSERT_TRUE(editor.Carrying().has_value());
    AimAway(1);
    Frame({}, {.click = true});
    Frame({});
    EXPECT_FALSE(editor.Carrying().has_value());
    EXPECT_EQ(editor.WatchDesign().Document().Components().size(), elements + 1);
}

TEST_F(EditorWorkspace, ThePreviewMovesElementsWithThePointer) {
    auto& editor = Open();
    editor.WatchDesign().SetSnap(false);
    const auto& config = editor.WatchDesign().Document();
    const auto index = config.Components().size() - 1;
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
    KeyboardInput input;
    input.SetTypingKeymap(std::make_shared<const Keymap>(std::move(*us)));
    const auto tap = [&input](VirtualKey key) -> void {
        input.Press(key, PointerMode::Left);
        input.Release(key);
    };

    std::string committed;
    editor.Text().Begin("test", "", [&committed](const std::string& text) -> void { committed = text; });
    ScopedRecordingHid hid;
    tap(VirtualKey::I);
    tap(VirtualKey::BackSpace);
    tap(VirtualKey::LShift);
    tap(VirtualKey::H);
    tap(VirtualKey::I);
    ASSERT_TRUE(editor.Text().Editing());
    EXPECT_EQ(editor.Text().Buffer(), "Hi");
    tap(VirtualKey::Return);
    EXPECT_EQ(committed, "Hi");
    EXPECT_FALSE(editor.Text().Editing());
    EXPECT_EQ(g_interaction->FocusedTextInput(), nullptr);
    EXPECT_TRUE(hid.Events().empty());

    tap(VirtualKey::H);
    EXPECT_EQ(hid.Events().size(), 2U);
}

TEST_F(EditorWorkspace, PressingAnotherPanelKeepsWhatWasTyped) {
    auto& editor = Open();
    const auto index = editor.WatchDesign().Document().Components().size() - 1;
    editor.WatchDesign().SetSelected(index);
    editor.Text().Begin("button.text", "", [&editor, index](const std::string& text) -> void {
        editor.EditWatch([index, &text](FormDocument& config) -> void {
            std::get<ButtonElement>(config.Components().at(index)).text = text;
        });
    });
    editor.Text().Key(VirtualKey::O, 0, "O");
    editor.Text().Key(VirtualKey::K, 0, "k");
    AimAt(1, PanelKind::Toolbar, {0.5f, 0.5f});
    Frame({});
    Frame({}, {.click = true});
    EXPECT_FALSE(editor.Text().Editing());
    EXPECT_EQ(std::get<ButtonElement>(editor.WatchDesign().Document().Components().at(index)).text, "Ok");
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

    KeyboardInput input;
    editor.Text().Capture(false, [&editor, index](const std::vector<VirtualKey>& keys) -> void {
        editor.EditKeyboard([index, &keys](KeyboardLayout& layout) -> void {
            layout.Keys().at(index).action = TypeKey{.key = keys.back()};
        });
    });
    EXPECT_TRUE(g_interaction->FocusedTextInput()->WantsRawKeys());
    input.Press(VirtualKey::Z, PointerMode::Left);
    input.Release(VirtualKey::Z);
    const auto* typed = std::get_if<TypeKey>(&editor.KeyboardDesign().Document().Keys().at(index).action);
    ASSERT_NE(typed, nullptr);
    EXPECT_EQ(typed->key, VirtualKey::Z);
    EXPECT_FALSE(editor.Text().Capturing());
}

TEST_F(EditorWorkspace, SaveWritesTheWatchAndRevertRemovesIt) {
    if (!ScratchConfig()) {
        GTEST_SKIP() << "writes config files, set XDG_CONFIG_HOME to a scratch directory";
    }
    auto& editor = Open();
    const float width = editor.WatchDesign().Document().Width();
    editor.EditWatch([](FormDocument& config) -> void { config.SetWidth(config.Width() + 0.01f); });
    editor.Save();
    ASSERT_TRUE(std::filesystem::exists(Forms::WatchFile("default")));
    EXPECT_NEAR(Forms::Watch("default").Width(), width + 0.01f, 1e-5f);
    EXPECT_FALSE(editor.Dirty());

    editor.Revert();
    EXPECT_TRUE(std::filesystem::exists(Forms::WatchFile("default")));
    editor.Revert();
    EXPECT_FALSE(std::filesystem::exists(Forms::WatchFile("default")));
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
    EXPECT_TRUE(std::filesystem::exists(KeyboardLayout::UserDirectory() / "keychron-v1-ansi-custom.yaml"));
    const auto& saved = KeyboardLayout::Find("keychron-v1-ansi-custom");
    EXPECT_EQ(saved.Name(), "keychron-v1-ansi-custom");
    EXPECT_FLOAT_EQ(saved.Columns(), 17.0f);
    EXPECT_EQ(g_config->keyboard_profile, "keychron-v1-ansi-custom");
    RunTasks();
    const auto meta = m_overlays->ByName(kKeyboardName)->Composer()->Meta();
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->extent.width, (17U * 80U) + 28U);
    EXPECT_FALSE(editor.Dirty());
}

TEST_F(EditorWorkspace, DoneAsksBeforeDroppingUnsavedWork) {
    auto& editor = Open();
    const auto controls = LiveWatchControls();
    editor.WatchDesign().Insert(FormPart::Panel, std::nullopt);
    editor.EditWatch([](FormDocument&) -> void {});
    Step();
    EXPECT_GT(LiveWatchControls(), controls);

    editor.Done();
    EXPECT_FALSE(editor.Closing());
    editor.Discard();
    EXPECT_TRUE(editor.Closing());
    Step();
    EXPECT_EQ(m_overlays->ActiveEditor(), nullptr);
    for (const auto kind : PanelPlacement::kPanels) {
        EXPECT_EQ(Panel(kind), nullptr);
    }
    EXPECT_EQ(LiveWatchControls(), controls);
}

TEST_F(EditorWorkspace, LayersSpreadInDepthAndLetYouReachThrough) {
    auto& editor = Open();
    editor.SetExploded(true);
    Step();
    EXPECT_FALSE(Panel(PanelKind::Preview)->WantVisible());
    const float back = Panel(PanelKind::BackLayer)->Transform().translation().z();
    const float front = Panel(PanelKind::FrontLayer)->Transform().translation().z();
    EXPECT_TRUE(Panel(PanelKind::MiddleLayer)->WantVisible());
    EXPECT_NEAR(front - back, 2.0f * PanelPlacement::kLayerSpacing, 1e-4f);

    const Eigen::Vector2f size = editor.PanelSize(PanelKind::Preview);
    const auto uv = [&](float x, float y) -> Eigen::Vector2f { return editor.WatchDesign().ToPreview({x, y}).cwiseQuotient(size); };
    AimAt(1, PanelKind::FrontLayer, uv(150.0f, 150.0f));
    Frame({});
    Frame({}, {.click = true});
    Frame({});
    ASSERT_TRUE(editor.WatchDesign().Selected().has_value());
    EXPECT_TRUE(
        std::holds_alternative<PanelElement>(editor.WatchDesign().Document().Components().at(*editor.WatchDesign().Selected()))
    );
    EXPECT_EQ(g_interaction->Pointers()[1].Interaction().hovered_id, Panel(PanelKind::BackLayer)->Id());

    AimAt(1, PanelKind::FrontLayer, uv(350.0f, 68.0f));
    Frame({});
    Frame({}, {.click = true});
    Frame({});
    ASSERT_TRUE(editor.WatchDesign().Selected().has_value());
    EXPECT_TRUE(
        std::holds_alternative<ButtonElement>(editor.WatchDesign().Document().Components().at(*editor.WatchDesign().Selected()))
    );
    EXPECT_EQ(g_interaction->Pointers()[1].Interaction().hovered_id, Panel(PanelKind::FrontLayer)->Id());

    RenderPanel(PanelKind::BackLayer, "editor-layer-back");
    RenderPanel(PanelKind::MiddleLayer, "editor-layer-middle");
    RenderPanel(PanelKind::FrontLayer, "editor-layer-front");
}

TEST_F(EditorWorkspace, OnlyOneHandUsesTheEditorAtATime) {
    auto& editor = Open();
    editor.WatchDesign().SetSnap(false);
    const auto index = editor.WatchDesign().Document().Components().size() - 1;
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
    g_interaction->SetHmd(Eigen::Affine3f(Eigen::AngleAxisf(std::numbers::pi_v<float> / 2.0f, Eigen::Vector3f::UnitY())));
    m_overlays->Recenter();
    Step();
    EXPECT_LT(Panel(PanelKind::Preview)->Transform().translation().x(), -0.5f);

    m_overlays->ShowHide();
    Step();
    EXPECT_FALSE(Panel(PanelKind::Preview)->WantVisible());
    EXPECT_FALSE(Panel(PanelKind::Toolbar)->WantVisible());
    m_overlays->ShowHide();
    Step();
    EXPECT_TRUE(Panel(PanelKind::Preview)->WantVisible());
    EXPECT_TRUE(Panel(PanelKind::Toolbar)->WantVisible());
}

TEST_F(EditorWorkspace, PickingAFontChangesTheTheme) {
    auto& editor = Open();
    editor.SwitchTarget(EditTarget::Theme);
    editor.OpenFonts();
    Step();
    const auto& families = g_fonts->Families();
    ASSERT_FALSE(families.empty());
    const auto preferred = std::ranges::find(families, std::string("DejaVu Serif"));
    const auto family = preferred != families.end() ? *preferred : families.front();
    auto* row = Panel(PanelKind::Dialog)->Composer()->Root()->Find(family);
    ASSERT_NE(row, nullptr);
    row->OnRelease();
    EXPECT_EQ(editor.ThemeHistory().Current().font, family);
    EXPECT_EQ(editor.Dialog(), DialogKind::None);
    Step();
    RenderPanel(PanelKind::Properties, "editor-theme-font");
    editor.OpenFonts();
    Step();
    Panel(PanelKind::Dialog)->Composer()->Root()->Find("default")->OnRelease();
    EXPECT_FALSE(editor.ThemeHistory().Current().font.has_value());
    g_fonts->SetFamily(std::nullopt);
}

TEST_F(EditorWorkspace, ClickingASwatchPicksIt) {
    auto& editor = Open();
    const auto index = editor.WatchDesign().Document().Components().size() - 1;
    const auto color = [&editor, index]() -> std::string {
        return std::get<ButtonElement>(editor.WatchDesign().Document().Components().at(index)).bg_color;
    };
    editor.OpenColor({
        .title = "Colour",
        .get = color,
        .set = [&editor, index](const std::string& value) -> void {
            editor.EditWatch([index, &value](FormDocument& config) -> void {
                std::get<ButtonElement>(config.Components().at(index)).bg_color = value;
            });
        },
    });
    Step();
    auto* swatch = Panel(PanelKind::Dialog)->Composer()->Root()->Find("@red");
    ASSERT_NE(swatch, nullptr);
    Eigen::Vector2f corner = Eigen::Vector2f::Zero();
    for (const auto* node = swatch; node != nullptr; node = node->Parent()) {
        corner += node->Position();
    }
    const Eigen::Vector2f center = corner + swatch->Size() / 2.0f;
    AimAt(1, PanelKind::Dialog, center.cwiseQuotient(editor.PanelSize(PanelKind::Dialog)));
    Frame({});
    Frame({}, {.click = true});
    Frame({});
    EXPECT_EQ(color(), "@red");
}

TEST_F(EditorWorkspace, TypingAHexValueSetsTheColour) {
    auto& editor = Open();
    const auto index = editor.WatchDesign().Document().Components().size() - 1;
    const auto color = [&editor, index]() -> std::string {
        return std::get<ButtonElement>(editor.WatchDesign().Document().Components().at(index)).bg_color;
    };
    editor.OpenColor({
        .title = "Colour",
        .get = color,
        .set = [&editor, index](const std::string& value) -> void {
            editor.EditWatch([index, &value](FormDocument& config) -> void {
                std::get<ButtonElement>(config.Components().at(index)).bg_color = value;
            });
        },
    });
    Step();
    const auto type = [&editor, this](std::string_view text) -> void {
        auto* hex = Panel(PanelKind::Dialog)->Composer()->Root()->Find("color.hex");
        ASSERT_NE(hex, nullptr);
        auto* field = hex->Find("field");
        ASSERT_NE(field, nullptr);
        field->OnRelease();
        ASSERT_TRUE(editor.Text().Editing("color.hex"));
        while (!editor.Text().Buffer().empty()) {
            editor.Text().Key(VirtualKey::BackSpace, 0, "");
        }
        for (const char c : text) {
            editor.Text().Key(VirtualKey::A, 0, std::string(1, c));
        }
        editor.Text().Key(VirtualKey::Return, 0, "");
        Step();
    };
    type("ff8800");
    EXPECT_EQ(color(), "#ff8800");
    type("@accent");
    EXPECT_EQ(color(), "@accent");
    type("nope");
    EXPECT_EQ(color(), "@accent");
    EXPECT_FALSE(editor.Text().Editing());
}

TEST_F(EditorWorkspace, TheColourWheelPicksLiveAndUndoesInOneStep) {
    auto& editor = Open();
    const auto index = editor.WatchDesign().Document().Components().size() - 1;
    const auto color = [&editor, index]() -> std::string {
        return std::get<ButtonElement>(editor.WatchDesign().Document().Components().at(index)).bg_color;
    };
    const auto before = color();
    editor.OpenColor({
        .title = "Colour",
        .get = [&editor, index]() -> std::string {
            return std::get<ButtonElement>(editor.WatchDesign().Document().Components().at(index)).bg_color;
        },
        .set = [&editor, index](const std::string& value) -> void {
            editor.EditWatch([index, &value](FormDocument& config) -> void {
                std::get<ButtonElement>(config.Components().at(index)).bg_color = value;
            });
        },
    });
    editor.Colors().SetHsv({0.0f, 0.0f, 1.0f});
    Step();
    const auto& wheel = editor.Colors().WheelRect();
    ASSERT_GT(wheel.w, 0.0f);
    const Eigen::Vector2f center(wheel.x + wheel.w / 2.0f, wheel.y + wheel.h / 2.0f);
    EXPECT_TRUE(editor.Colors().Pointer(editor, center + Eigen::Vector2f(wheel.w / 2.0f, 0.0f), true));
    EXPECT_EQ(color(), "#ff0000");
    EXPECT_TRUE(editor.Colors().Hover(editor, center));
    EXPECT_EQ(color(), "#ffffff");
    EXPECT_TRUE(editor.Colors().Pointer(editor, center, false));
    RenderPanel(PanelKind::Dialog, "editor-color-wheel-bright");
    const auto& bar = editor.Colors().BarRect();
    EXPECT_TRUE(editor.Colors().Pointer(editor, {bar.x, bar.y + bar.h / 2.0f}, true));
    EXPECT_TRUE(editor.Colors().Pointer(editor, {bar.x, bar.y + bar.h / 2.0f}, false));
    EXPECT_EQ(color(), "#000000");
    EXPECT_FALSE(editor.Colors().Pointer(editor, {1.0f, 1.0f}, true));
    RenderPanel(PanelKind::Dialog, "editor-color-wheel");

    editor.Undo();
    EXPECT_EQ(color(), "#ffffff");
    editor.Undo();
    EXPECT_EQ(color(), before);
}

TEST_F(EditorWorkspace, ExtraFormsGrowTheLiveWatch) {
    auto& editor = Open();
    const auto width = m_overlays->ByName(kWatchName)->Composer()->Meta()->extent.width;
    editor.WatchDesign().Insert(FormPart::Form, std::nullopt);
    editor.EditWatch([](FormDocument&) -> void {});
    Step();
    EXPECT_GT(m_overlays->ByName(kWatchName)->Composer()->Meta()->extent.width, width);
    RenderPanel(PanelKind::Preview, "editor-extra-form");
    RenderPanel(PanelKind::Properties, "editor-extra-form-properties");
}

TEST_F(EditorWorkspace, TimezonesAreSearchedAndPicked) {
    auto& editor = Open();
    const auto& elements = editor.WatchDesign().Document().Components();
    const auto clock = std::ranges::find_if(elements, [](const FormElement& element) -> bool {
        return std::holds_alternative<ClockElement>(element);
    });
    ASSERT_NE(clock, elements.end());
    const auto index = static_cast<size_t>(clock - elements.begin());
    editor.WatchDesign().SetSelected(index);
    editor.OpenTimezones(index);
    editor.Text().Begin("timezone.search", "", [](const std::string&) -> void {});
    for (const auto* letter : {"h", "e", "l", "s"}) {
        editor.Text().Key(VirtualKey::A, 0, letter);
    }
    Step();
    RenderPanel(PanelKind::Dialog, "editor-timezones");

    auto* canvas = Panel(PanelKind::Dialog)->Composer();
    size_t results = 0;
    for (const auto& item : canvas->Items()) {
        const auto* text = std::get_if<TextItem>(&item);
        if (text != nullptr
            && (text->text.starts_with("Europe/") || text->text.starts_with("Asia/") || text->text.starts_with("America/"))) {
            ++results;
        }
    }
    auto* helsinki = canvas->FindText("Europe/Helsinki");
    ASSERT_NE(helsinki, nullptr);
    EXPECT_LE(results, 3U);
    helsinki->OnRelease();
    const auto& zone = std::get<ClockElement>(editor.WatchDesign().Document().Components().at(index)).timezone;
    ASSERT_TRUE(zone.has_value());
    EXPECT_EQ(std::get<std::string>(*zone), "Europe/Helsinki");
    EXPECT_EQ(editor.Dialog(), DialogKind::None);
    EXPECT_EQ(g_interaction->FocusedTextInput(), nullptr);
}

TEST_F(EditorWorkspace, TheEditButtonClosesTheEditorAndKeepsADraft) {
    auto& editor = Open();
    const auto controls = LiveWatchControls();
    const auto elements = editor.WatchDesign().Document().Components().size();
    editor.WatchDesign().Insert(FormPart::Panel, std::nullopt);
    editor.EditWatch([](FormDocument&) -> void {});
    Step();
    EXPECT_GT(LiveWatchControls(), controls);

    m_overlays->OpenEditor();
    Step();
    EXPECT_EQ(m_overlays->ActiveEditor(), nullptr);
    EXPECT_TRUE(std::filesystem::exists(Drafts::File(EditTarget::Watch)));
    EXPECT_EQ(LiveWatchControls(), controls);

    auto& reopened = Open();
    EXPECT_EQ(reopened.WatchDesign().Document().Components().size(), elements + 1);
    EXPECT_TRUE(reopened.Dirty());
    EXPECT_GT(LiveWatchControls(), controls);
    reopened.Undo();
    EXPECT_FALSE(reopened.Dirty());
    reopened.Redo();
    reopened.Save();
    EXPECT_FALSE(std::filesystem::exists(Drafts::File(EditTarget::Watch)));
}

TEST_F(EditorWorkspace, DraftsSurviveACrash) {
    auto& editor = Open();
    editor.SwitchTarget(EditTarget::Keyboard);
    editor.KeyboardDesign().AddColumn();
    editor.WatchDesign().Insert(FormPart::Button, std::nullopt);
    editor.DraftFiles().Save(editor, true);
    Recreate();

    auto& recovered = Open();
    EXPECT_FLOAT_EQ(recovered.KeyboardDesign().Document().Columns(), 17.0f);
    EXPECT_TRUE(recovered.KeyboardDesign().Revisions().Dirty());
    EXPECT_TRUE(recovered.WatchDesign().Revisions().Dirty());
    recovered.Discard();
    Step();
    EXPECT_FALSE(std::filesystem::exists(Drafts::File(EditTarget::Watch)));
    EXPECT_FALSE(std::filesystem::exists(Drafts::File(EditTarget::Keyboard)));
}

TEST_F(EditorWorkspace, CityClocksAreOneComponentMeasuredByWhatTheyShow) {
    auto& editor = Open();
    const auto& config = editor.WatchDesign().Document();
    size_t clocks = 0;
    for (size_t i = 0; i < config.Components().size(); ++i) {
        const auto* clock = std::get_if<ClockElement>(&config.Components()[i]);
        if (clock == nullptr || !clock->city_size) {
            continue;
        }
        ++clocks;
        const auto rect = editor.WatchDesign().VisualRect(i);
        const auto& placed = FormElements::Rect(config.Components()[i]);
        EXPECT_LT(
            rect.y,
            editor.WatchDesign().FormBounds(0).y + placed[1] - static_cast<float>(clock->font_size)
                - static_cast<float>(*clock->city_size)
        );
        EXPECT_GT(rect.w, 20.0f);
        EXPECT_LT(rect.w, 110.0f);
        EXPECT_LE(rect.x + rect.w, editor.WatchDesign().FormBounds(0).x + editor.WatchDesign().FormBounds(0).w);
        EXPECT_EQ(editor.WatchDesign().HitTest({rect.x + 5.0f, rect.y + 3.0f}).index, i);
    }
    EXPECT_EQ(clocks, 2U);
    EXPECT_FALSE(std::ranges::any_of(config.Components(), [](const FormElement& element) -> bool {
        return std::holds_alternative<TimezoneElement>(element);
    }));
}

TEST_F(EditorWorkspace, ThemesRecolourEverythingThatUsesThem) {
    auto& editor = Open();
    EXPECT_EQ(FormTheme::ParseColor("@accent").value(), FormTheme::ParseColor("#c6a0f6").value());
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
    EXPECT_EQ(FormTheme::ParseColor("@accent").value(), FormTheme::ParseColor("#00ff00").value());
    EXPECT_EQ(g_themes->Active().rounding, Rounding::Square);
    EXPECT_TRUE(editor.Dirty());

    editor.NewTheme();
    EXPECT_TRUE(editor.ThemeHistory().Dirty());
    editor.Save();
    editor.NewTheme();
    EXPECT_EQ(editor.ThemeHistory().Current().name, "my-theme");
    EXPECT_EQ(g_config->theme, "my-theme");
    EXPECT_TRUE(std::filesystem::exists(FormTheme::UserDirectory() / "my-theme.yaml"));
    EXPECT_EQ(FormTheme::Lookup(g_themes->Find("my-theme"), "accent"), std::optional<std::string>("#00ff00"));

    editor.SelectTheme("latte");
    EXPECT_EQ(FormTheme::ParseColor("@background").value(), FormTheme::ParseColor("#eff1f5").value());
    Step();
    RenderPanel(PanelKind::Preview, "editor-theme-latte");
    editor.SelectTheme("my-theme");
    editor.DeleteTheme();
    editor.DeleteTheme();
    EXPECT_FALSE(std::filesystem::exists(FormTheme::UserDirectory() / "my-theme.yaml"));
    EXPECT_EQ(editor.ThemeHistory().Current().name, "macchiato");
    editor.Revert();
    editor.Revert();
    EXPECT_EQ(FormTheme::ParseColor("@accent").value(), FormTheme::ParseColor("#c6a0f6").value());
}

TEST_F(EditorWorkspace, WatchLayoutsAreChosenCreatedAndDeleted) {
    auto& editor = Open();
    const auto elements = editor.WatchDesign().Document().Components().size();
    editor.NewWatch();
    EXPECT_EQ(editor.WatchProfile(), "watch-2");
    EXPECT_EQ(g_config->watch_profile, "watch-2");
    editor.WatchDesign().Insert(FormPart::Panel, std::nullopt);
    editor.Save();
    EXPECT_EQ(Forms::Watch("watch-2").Components().size(), elements + 1);

    editor.SelectWatch("default");
    EXPECT_EQ(editor.WatchDesign().Document().Components().size(), elements);
    editor.SelectWatch("watch-2");
    EXPECT_EQ(editor.WatchDesign().Document().Components().size(), elements + 1);
    RenderPanel(PanelKind::Properties, "editor-watch-layouts");
    editor.RenameWatch("mine");
    EXPECT_EQ(Forms::WatchProfiles(), (std::vector<std::string>{"default", "mine"}));
    editor.DeleteWatch();
    editor.DeleteWatch();
    EXPECT_EQ(Forms::WatchProfiles(), std::vector<std::string>{"default"});
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
        std::ofstream(Forms::WatchFile(name)) << "width: 0.1\nform: {width: 100, height: 50}\ncomponents: []\n";
    }
    editor.Touch();
    Step();
    auto* canvas = Panel(PanelKind::Properties)->Composer();
    bool shortened = false;
    for (const auto& item : canvas->Items()) {
        const auto* label = std::get_if<TextItem>(&item);
        if (label == nullptr) {
            continue;
        }
        const auto& text = label->text;
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
    editor.WatchDesign().SetSelected(editor.WatchDesign().Document().Components().size() - 2);
    editor.Touch();
    RenderPanel(PanelKind::Preview, "editor-watch-preview");
    RenderPanel(PanelKind::Toolbar, "editor-toolbar");
    RenderPanel(PanelKind::Palette, "editor-watch-palette");
    RenderPanel(PanelKind::Properties, "editor-watch-properties");
    editor.OpenActions(ButtonTrigger::Click);
    RenderPanel(PanelKind::Dialog, "editor-actions");
    const auto& elements = editor.WatchDesign().Document().Components();
    const auto clock = std::ranges::find_if(elements, [](const FormElement& element) -> bool {
        return std::holds_alternative<ClockElement>(element);
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
        .get = []() -> std::string { return "#8aadf4"; },
        .set = [](const std::string&) -> void {},
    });
    RenderPanel(PanelKind::Dialog, "editor-color");
    editor.KeyboardDesign().Selected().reset();
    editor.Touch();
    RenderPanel(PanelKind::Properties, "editor-keyboard-properties");
}
