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

#include "panels/ToolbarPanel.hpp"

#include <vector>

#include "ButtonComponent.hpp"
#include "Editor.hpp"
#include "FontCache.hpp"
#include "RowComponent.hpp"
#include "StackComponent.hpp"
#include "TextLineComponent.hpp"
#include "panels/PanelStyle.hpp"

ToolbarPanel::ToolbarPanel(Editor& editor, const Eigen::Vector2f& size)
    : FormComponent("toolbar"),
      m_stack(AddComponent<StackComponent>("stack", PanelStyle::kPadding, PanelStyle::kGap)) {
    SetSize(size);
    m_stack.SetSize(size);
    m_stack.SetBgColor(FormTheme::Rgba("@panel"));
    if (editor.ClosingPrompt()) {
        AddClosingPrompt(editor);
    } else {
        AddTools(editor);
    }
}

auto ToolbarPanel::AddTools(Editor& editor) -> void {
    const auto text = FormTheme::Rgba("@text");
    const auto surface = FormTheme::Rgba("@surface");
    const auto shadow = FormTheme::Rgba("@shadow");
    const auto accent = FormTheme::Rgba("@accent");
    const auto on_accent = FormTheme::Rgba("@on-accent");
    const auto red = FormTheme::Rgba("@red");

    bool can_undo = editor.ThemeHistory().CanUndo();
    bool can_redo = editor.ThemeHistory().CanRedo();
    bool dirty = editor.ThemeHistory().Dirty();
    if (editor.Target() == EditTarget::Watch) {
        can_undo = editor.WatchDesign().Revisions().CanUndo();
        can_redo = editor.WatchDesign().Revisions().CanRedo();
        dirty = editor.WatchDesign().Revisions().Dirty();
    } else if (editor.Target() == EditTarget::Keyboard) {
        can_undo = editor.KeyboardDesign().Revisions().CanUndo();
        can_redo = editor.KeyboardDesign().Revisions().CanRedo();
        dirty = editor.KeyboardDesign().Revisions().Dirty();
    }

    auto& row = m_stack.AddComponent<RowComponent>("tools", PanelStyle::kRowHeight, PanelStyle::kGap);

    const bool on_watch = editor.Target() == EditTarget::Watch;
    auto& watch = row.AddComponent<ButtonComponent>("watch", "Watch");
    watch.SetFontSize(PanelStyle::kTextFont);
    watch.SetFgColor(on_watch ? on_accent : text);
    watch.SetBgColor(on_watch ? accent : surface);
    watch.SetEllipsize(true);
    watch.SetOnRelease([&editor]() -> void { editor.SwitchTarget(EditTarget::Watch); });

    const bool on_keyboard = editor.Target() == EditTarget::Keyboard;
    auto& keyboard = row.AddComponent<ButtonComponent>("keyboard", "Keyboard");
    keyboard.SetFontSize(PanelStyle::kTextFont);
    keyboard.SetFgColor(on_keyboard ? on_accent : text);
    keyboard.SetBgColor(on_keyboard ? accent : surface);
    keyboard.SetEllipsize(true);
    keyboard.SetOnRelease([&editor]() -> void { editor.SwitchTarget(EditTarget::Keyboard); });

    const bool on_theme = editor.Target() == EditTarget::Theme;
    auto& theme = row.AddComponent<ButtonComponent>("theme", "Theme");
    theme.SetFontSize(PanelStyle::kTextFont);
    theme.SetFgColor(on_theme ? on_accent : text);
    theme.SetBgColor(on_theme ? accent : surface);
    theme.SetEllipsize(true);
    theme.SetOnRelease([&editor]() -> void { editor.SwitchTarget(EditTarget::Theme); });

    auto& undo = row.AddComponent<ButtonComponent>("undo", "Undo");
    undo.SetFontSize(PanelStyle::kTextFont);
    undo.SetFgColor(text);
    undo.SetBgColor(can_undo ? surface : shadow);
    undo.SetEllipsize(true);
    undo.SetOnRelease([&editor]() -> void { editor.Undo(); });

    auto& redo = row.AddComponent<ButtonComponent>("redo", "Redo");
    redo.SetFontSize(PanelStyle::kTextFont);
    redo.SetFgColor(text);
    redo.SetBgColor(can_redo ? surface : shadow);
    redo.SetEllipsize(true);
    redo.SetOnRelease([&editor]() -> void { editor.Redo(); });

    if (on_watch) {
        const bool snapping = editor.WatchDesign().Snap();
        auto& snap = row.AddComponent<ButtonComponent>("snap", "Snap");
        snap.SetFontSize(PanelStyle::kTextFont);
        snap.SetFgColor(snapping ? on_accent : text);
        snap.SetBgColor(snapping ? accent : surface);
        snap.SetEllipsize(true);
        snap.SetOnRelease([&editor]() -> void {
            editor.WatchDesign().SetSnap(!editor.WatchDesign().Snap());
            editor.Touch();
        });

        auto& layers = row.AddComponent<ButtonComponent>("layers", "Layers");
        layers.SetFontSize(PanelStyle::kTextFont);
        layers.SetFgColor(editor.Layers() ? on_accent : text);
        layers.SetBgColor(editor.Layers() ? accent : surface);
        layers.SetEllipsize(true);
        layers.SetOnRelease([&editor]() -> void { editor.SetExploded(!editor.Layers()); });
    }

    const bool on_hand = editor.Placement().PaletteOnHand();
    auto& hand = row.AddComponent<ButtonComponent>("hand", "On hand");
    hand.SetFontSize(PanelStyle::kTextFont);
    hand.SetFgColor(on_hand ? on_accent : text);
    hand.SetBgColor(on_hand ? accent : surface);
    hand.SetEllipsize(true);
    hand.SetOnRelease([&editor]() -> void {
        editor.Placement().SetPaletteOnHand(!editor.Placement().PaletteOnHand());
        editor.Touch();
    });

    auto& save = row.AddComponent<ButtonComponent>("save", dirty ? "Save*" : "Save");
    save.SetFontSize(PanelStyle::kTextFont);
    save.SetFgColor(dirty ? on_accent : text);
    save.SetBgColor(dirty ? accent : surface);
    save.SetEllipsize(true);
    save.SetOnRelease([&editor]() -> void { editor.Save(); });

    auto& revert = row.AddComponent<ButtonComponent>("revert", editor.RevertArmed() ? "Sure?" : "Revert");
    revert.SetFontSize(PanelStyle::kTextFont);
    revert.SetFgColor(editor.RevertArmed() ? on_accent : text);
    revert.SetBgColor(editor.RevertArmed() ? red : surface);
    revert.SetEllipsize(true);
    revert.SetOnRelease([&editor]() -> void { editor.Revert(); });

    auto& done = row.AddComponent<ButtonComponent>("done", "Done");
    done.SetFontSize(PanelStyle::kTextFont);
    done.SetFgColor(text);
    done.SetBgColor(surface);
    done.SetEllipsize(true);
    done.SetOnRelease([&editor]() -> void { editor.Done(); });

    std::vector<float> weights;
    for (const auto& child : row.Children()) {
        const auto& button = dynamic_cast<const ButtonComponent&>(*child);
        weights.push_back(FontCache::Width(button.Text(), PanelStyle::kTextFont) + 24.0f);
    }
    row.SetWeights(std::move(weights));
}

auto ToolbarPanel::AddClosingPrompt(Editor& editor) -> void {
    const auto text = FormTheme::Rgba("@text");
    const auto subtle = FormTheme::Rgba("@subtle");
    const auto surface = FormTheme::Rgba("@surface");
    const auto on_accent = FormTheme::Rgba("@on-accent");
    const auto red = FormTheme::Rgba("@red");

    auto& row = m_stack.AddComponent<RowComponent>("prompt", PanelStyle::kRowHeight, PanelStyle::kGap);
    row.SetWeights({1.6f, 1.0f, 1.0f, 1.0f, 1.0f});

    row.AddComponent<TextLineComponent>("message", "Unsaved changes", PanelStyle::kTextFont, subtle);

    auto& save = row.AddComponent<ButtonComponent>("save", "Save all & close");
    save.SetFontSize(PanelStyle::kTextFont);
    save.SetFgColor(text);
    save.SetBgColor(surface);
    save.SetEllipsize(true);
    save.SetOnRelease([&editor]() -> void { editor.SaveAll(); });

    auto& keep = row.AddComponent<ButtonComponent>("keep", "Keep draft");
    keep.SetFontSize(PanelStyle::kTextFont);
    keep.SetFgColor(text);
    keep.SetBgColor(surface);
    keep.SetEllipsize(true);
    keep.SetOnRelease([&editor]() -> void { editor.Suspend(); });

    auto& discard = row.AddComponent<ButtonComponent>("discard", "Discard");
    discard.SetFontSize(PanelStyle::kTextFont);
    discard.SetFgColor(on_accent);
    discard.SetBgColor(red);
    discard.SetEllipsize(true);
    discard.SetOnRelease([&editor]() -> void { editor.Discard(); });

    auto& resume = row.AddComponent<ButtonComponent>("resume", "Keep editing");
    resume.SetFontSize(PanelStyle::kTextFont);
    resume.SetFgColor(text);
    resume.SetBgColor(surface);
    resume.SetEllipsize(true);
    resume.SetOnRelease([&editor]() -> void { editor.KeepEditing(); });
}
