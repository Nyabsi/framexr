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

#include "EditorPanel.hpp"

#include <cmath>
#include <string>

#include "Editor.hpp"
#include "Form.hpp"
#include "Interaction.hpp"
#include "Logger.hpp"
#include "panels/ActionsPanel.hpp"
#include "panels/CapturePanel.hpp"
#include "panels/ColorPanel.hpp"
#include "panels/FontPanel.hpp"
#include "panels/GhostPanel.hpp"
#include "panels/KeyPickerPanel.hpp"
#include "panels/KeyboardProperties.hpp"
#include "panels/PalettePanel.hpp"
#include "panels/ThemeProperties.hpp"
#include "panels/TimezonePanel.hpp"
#include "panels/ToolbarPanel.hpp"
#include "panels/WatchProperties.hpp"

EditorPanel::EditorPanel(const std::shared_ptr<Editor>& editor, PanelKind kind) : m_editor(editor), m_kind(kind)
{
    const Eigen::Vector2f size = editor->PanelSize(kind).array().round();
    SetName(std::string(PanelPlacement::PanelName(kind)));
    SetWantVisible(kind != PanelKind::Dialog && kind != PanelKind::Ghost && !PanelPlacement::LayerOf(kind));
    SetInteractable(kind != PanelKind::Ghost);
    SetGrabbable(kind != PanelKind::Ghost);
    SetAttachedTo(RelativeTo::Stage);
    SetZOrder(kind == PanelKind::Ghost ? kZOrderWatch : kZOrderDefault);
    SetTransform(editor->Placement().Anchor() * PanelPlacement::LocalTransform(*editor, kind));
    SetInteractionTransform(UiTransform({static_cast<uint32_t>(size.x()), static_cast<uint32_t>(size.y())}));
    Update();
}

auto EditorPanel::Update() -> void
{
    const auto editor = m_editor.lock();
    if (!editor || editor->Revision(m_kind) == m_built)
    {
        return;
    }
    m_built = editor->Revision(m_kind);
    Rebuild(*editor);
}

auto EditorPanel::Rebuild(Editor& editor) -> void
{
    const Eigen::Vector2f size = editor.PanelSize(m_kind).array().round();
    auto form = std::make_unique<Form>(std::string(PanelPlacement::PanelName(m_kind)));
    form->SetSize(size);
    switch (m_kind)
    {
    case PanelKind::Preview:
        if (editor.Target() == EditTarget::Keyboard)
        {
            editor.KeyboardDesign().Draw(*form, g_interaction->TypingKeymap());
        }
        else
        {
            editor.WatchDesign().Draw(*form);
        }
        break;
    case PanelKind::BackLayer:
    case PanelKind::MiddleLayer:
    case PanelKind::FrontLayer: editor.WatchDesign().Draw(*form, PanelPlacement::LayerOf(m_kind)); break;
    case PanelKind::Ghost: form->AddComponent<GhostPanel>(editor, size); break;
    case PanelKind::Toolbar: form->AddComponent<ToolbarPanel>(editor, size); break;
    case PanelKind::Palette: form->AddComponent<PalettePanel>(editor, size); break;
    case PanelKind::Properties:
        if (editor.Text().Capturing())
        {
            form->AddComponent<CapturePanel>(editor, size);
        }
        else if (editor.Target() == EditTarget::Watch)
        {
            form->AddComponent<WatchProperties>(editor, size);
        }
        else if (editor.Target() == EditTarget::Keyboard)
        {
            form->AddComponent<KeyboardProperties>(editor, size);
        }
        else
        {
            form->AddComponent<ThemeProperties>(editor, size);
        }
        break;
    case PanelKind::Dialog:
        switch (editor.Dialog())
        {
        case DialogKind::Color: form->AddComponent<ColorPanel>(editor, size); break;
        case DialogKind::Actions: form->AddComponent<ActionsPanel>(editor, size); break;
        case DialogKind::KeyPicker: form->AddComponent<KeyPickerPanel>(editor, size); break;
        case DialogKind::Timezone: form->AddComponent<TimezonePanel>(editor, size); break;
        case DialogKind::Font: form->AddComponent<FontPanel>(editor, size); break;
        case DialogKind::None: break;
        }
        break;
    }
    if (auto result = Composer()->SetForm(std::move(form)); !result)
    {
        LOG_ERROR("{}: {}", Name(), result.error().Message());
    }
}

auto EditorPanel::IsPreview() const -> bool
{
    return m_kind == PanelKind::Preview || PanelPlacement::LayerOf(m_kind).has_value();
}

auto EditorPanel::ToPanel(const PointerHit& hit) const -> Eigen::Vector2f
{
    const auto editor = m_editor.lock();
    return editor ? Eigen::Vector2f(hit.uv.cwiseProduct(editor->PanelSize(m_kind).array().round().matrix()))
                  : Eigen::Vector2f::Zero();
}

auto EditorPanel::Accepts(const Eigen::Vector2f& uv) const -> bool
{
    const auto editor = m_editor.lock();
    if (!editor || !PanelPlacement::LayerOf(m_kind))
    {
        return true;
    }
    return editor->Accepts(m_kind, uv.cwiseProduct(editor->PanelSize(m_kind)));
}

auto EditorPanel::OnHover(const PointerHit& hit) -> std::optional<Haptics>
{
    const auto editor = m_editor.lock();
    if (!editor || !editor->Allows(hit.pointer))
    {
        return std::nullopt;
    }
    if (IsPreview())
    {
        return editor->PreviewHover(ToPanel(hit));
    }
    if (m_kind == PanelKind::Dialog && editor->Dialog() == DialogKind::Color && editor->Colors().Hover(*editor, ToPanel(hit)))
    {
        return std::nullopt;
    }
    return View::OnHover(hit);
}

auto EditorPanel::OnLeft(size_t pointer) -> void
{
    const auto editor = m_editor.lock();
    if (!editor || !editor->Allows(pointer))
    {
        return;
    }
    if (IsPreview())
    {
        editor->PreviewLeft();
        return;
    }
    View::OnLeft(pointer);
}

auto EditorPanel::OnPointer(const PointerHit& hit, bool pressed) -> void
{
    const auto editor = m_editor.lock();
    if (!editor || !editor->Allows(hit.pointer))
    {
        return;
    }
    if (pressed)
    {
        editor->Claim(hit.pointer);
    }
    if (IsPreview())
    {
        editor->PreviewPointer(m_kind, hit, ToPanel(hit), pressed);
    }
    else if (!(m_kind == PanelKind::Dialog && editor->Dialog() == DialogKind::Color
               && editor->Colors().Pointer(*editor, ToPanel(hit), pressed)))
    {
        if (pressed)
        {
            editor->Text().Commit();
        }
        else
        {
            editor->PanelReleased(m_kind, hit);
        }
        View::OnPointer(hit, pressed);
    }
    if (!pressed)
    {
        editor->Unclaim(hit.pointer);
    }
}

auto EditorPanel::OnScroll(const PointerHit& hit, float delta_y, float delta_x) -> void
{
    const auto editor = m_editor.lock();
    if (!editor || !editor->Allows(hit.pointer))
    {
        return;
    }
    if (IsPreview())
    {
        return;
    }
    View::OnScroll(hit, delta_y, delta_x);
}
