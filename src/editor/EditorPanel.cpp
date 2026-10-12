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

#include "CommandBatch.hpp"
#include "Device.hpp"
#include "InteractionHandler.hpp"
#include "OverlayRenderer.hpp"

EditorPanel::EditorPanel(std::weak_ptr<Editor> editor, PanelKind kind) : m_editor(std::move(editor)), m_kind(kind) {
}

auto EditorPanel::SetRenderer(std::unique_ptr<OverlayRenderer>) -> void {
}

auto EditorPanel::SetInteraction(std::unique_ptr<InteractionHandler>) -> void {
}

auto EditorPanel::Init() -> Result<> {
    return Refresh();
}

auto EditorPanel::Pause() -> Result<> {
    return {};
}

auto EditorPanel::Resume() -> Result<> {
    return {};
}

auto EditorPanel::Refresh() -> Result<> {
    const auto editor = m_editor.lock();
    if (!editor) {
        return {};
    }
    const Eigen::Vector2f size = editor->PanelSize(m_kind);
    const auto width = static_cast<uint32_t>(std::lround(size.x()));
    const auto height = static_cast<uint32_t>(std::lround(size.y()));
    if (!m_canvas || m_canvas->Width() != width || m_canvas->Height() != height) {
        ASSIGN_OR_RETURN(m_canvas, Canvas::Create(width, height, g_device->NativeFormat()));
        m_built = 0;
    }
    if (const auto revision = editor->Revision(m_kind); revision != m_built) {
        m_canvas->Clear();
        editor->BuildPanel(m_kind, *m_canvas);
        m_built = revision;
    }
    return {};
}

auto EditorPanel::ShouldRender() -> Result<RenderReadiness> {
    RETURN_IF_ERROR(Refresh());
    if (!m_canvas) {
        return RenderReadiness::Unable;
    }
    return m_canvas->ShouldRender();
}

auto EditorPanel::Render(const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> {
    if (!m_canvas) {
        return false;
    }
    return m_canvas->Render(target, batch, alpha);
}

auto EditorPanel::Meta() -> std::optional<FrameMeta> {
    if (m_canvas) {
        return m_canvas->Meta();
    }
    const auto editor = m_editor.lock();
    if (!editor) {
        return std::nullopt;
    }
    const Eigen::Vector2f size = editor->PanelSize(m_kind);
    return FrameMeta{.extent = {static_cast<uint32_t>(std::lround(size.x())), static_cast<uint32_t>(std::lround(size.y()))}};
}

auto EditorPanel::IsPreview() const -> bool {
    return m_kind == PanelKind::Preview || Editor::LayerOf(m_kind).has_value();
}

auto EditorPanel::Accepts(const Eigen::Vector2f& uv) const -> bool {
    const auto editor = m_editor.lock();
    if (!editor || !Editor::LayerOf(m_kind)) {
        return true;
    }
    return editor->Accepts(m_kind, uv.cwiseProduct(editor->PanelSize(m_kind)));
}

auto EditorPanel::ToCanvas(const PointerHit& hit) const -> Eigen::Vector2f {
    if (!m_canvas) {
        return Eigen::Vector2f::Zero();
    }
    return hit.uv.cwiseProduct(Eigen::Vector2f(static_cast<float>(m_canvas->Width()), static_cast<float>(m_canvas->Height())));
}

auto EditorPanel::OnHover(const PointerHit& hit) -> std::optional<Haptics> {
    const auto editor = m_editor.lock();
    if (!editor || !m_canvas || !editor->Allows(hit.pointer)) {
        return std::nullopt;
    }
    if (IsPreview()) {
        return editor->PreviewHover(ToCanvas(hit));
    }
    if (m_kind == PanelKind::Dialog && editor->DialogHover(ToCanvas(hit))) {
        return std::nullopt;
    }
    return m_canvas->OnHover(hit);
}

auto EditorPanel::OnLeft(size_t pointer) -> void {
    const auto editor = m_editor.lock();
    if (!editor || !m_canvas || !editor->Allows(pointer)) {
        return;
    }
    if (IsPreview()) {
        editor->PreviewLeft();
        return;
    }
    m_canvas->OnLeft(pointer);
}

auto EditorPanel::OnPointer(const PointerHit& hit, bool pressed) -> void {
    const auto editor = m_editor.lock();
    if (!editor || !m_canvas || !editor->Allows(hit.pointer)) {
        return;
    }
    if (pressed) {
        editor->Claim(hit.pointer);
    }
    if (IsPreview()) {
        editor->PreviewPointer(m_kind, hit, ToCanvas(hit), pressed);
    } else if (!(m_kind == PanelKind::Dialog && editor->DialogPointer(ToCanvas(hit), pressed))) {
        if (pressed) {
            editor->CommitText();
        } else {
            editor->PanelReleased(m_kind, hit);
        }
        m_canvas->OnPointer(hit, pressed);
    }
    if (!pressed) {
        editor->Unclaim(hit.pointer);
    }
}

auto EditorPanel::OnScroll(const PointerHit& hit, float delta_y, float delta_x) -> void {
    const auto editor = m_editor.lock();
    if (!editor || !m_canvas || !editor->Allows(hit.pointer)) {
        return;
    }
    if (IsPreview()) {
        editor->PreviewScroll(ToCanvas(hit), delta_y);
        return;
    }
    m_canvas->OnScroll(hit, delta_y, delta_x);
}
