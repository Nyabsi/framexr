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

#include "Device.hpp"
#include "State.hpp"

EditorPanel::EditorPanel(std::weak_ptr<Editor> editor, PanelKind kind) : editor_(std::move(editor)), kind_(kind) {
}

auto EditorPanel::SetRenderer(std::unique_ptr<OverlayRenderer>) -> void {
}

auto EditorPanel::SetInteraction(std::unique_ptr<InteractionHandler>) -> void {
}

auto EditorPanel::Init(AppState& app) -> Result<> {
    return Refresh(app);
}

auto EditorPanel::Pause(AppState&) -> Result<> {
    return {};
}

auto EditorPanel::Resume(AppState&) -> Result<> {
    return {};
}

auto EditorPanel::Refresh(AppState& app) -> Result<> {
    const auto editor = editor_.lock();
    if (!editor) {
        return {};
    }
    const Eigen::Vector2f size = editor->PanelSize(kind_);
    const auto width = static_cast<uint32_t>(std::lround(size.x()));
    const auto height = static_cast<uint32_t>(std::lround(size.y()));
    if (!canvas_ || canvas_->Width() != width || canvas_->Height() != height) {
        ASSIGN_OR_RETURN(canvas_, Canvas::Create(app.device, width, height, app.device->NativeFormat()));
        built_ = 0;
    }
    if (const auto revision = editor->Revision(kind_); revision != built_) {
        canvas_->Clear();
        editor->BuildPanel(kind_, *canvas_, app);
        built_ = revision;
    }
    return {};
}

auto EditorPanel::ShouldRender(AppState& app) -> Result<RenderReadiness> {
    RETURN_IF_ERROR(Refresh(app));
    if (!canvas_) {
        return RenderReadiness::Unable;
    }
    return canvas_->ShouldRender(app);
}

auto EditorPanel::Render(AppState& app, const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> {
    if (!canvas_) {
        return false;
    }
    return canvas_->Render(app, target, batch, alpha);
}

auto EditorPanel::Meta() -> std::optional<FrameMeta> {
    if (canvas_) {
        return canvas_->Meta();
    }
    const auto editor = editor_.lock();
    if (!editor) {
        return std::nullopt;
    }
    const Eigen::Vector2f size = editor->PanelSize(kind_);
    return FrameMeta{.extent = {static_cast<uint32_t>(std::lround(size.x())), static_cast<uint32_t>(std::lround(size.y()))}};
}

auto EditorPanel::IsPreview() const -> bool {
    return kind_ == PanelKind::Preview || Editor::LayerOf(kind_).has_value();
}

auto EditorPanel::Accepts(const Eigen::Vector2f& uv) const -> bool {
    const auto editor = editor_.lock();
    if (!editor || !Editor::LayerOf(kind_)) {
        return true;
    }
    return editor->Accepts(kind_, uv.cwiseProduct(editor->PanelSize(kind_)));
}

auto EditorPanel::ToCanvas(const PointerHit& hit) const -> Eigen::Vector2f {
    if (!canvas_) {
        return Eigen::Vector2f::Zero();
    }
    return hit.uv.cwiseProduct(Eigen::Vector2f(static_cast<float>(canvas_->Width()), static_cast<float>(canvas_->Height())));
}

auto EditorPanel::OnHover(AppState& app, const PointerHit& hit) -> std::optional<Haptics> {
    const auto editor = editor_.lock();
    if (!editor || !canvas_ || !editor->Allows(hit.pointer)) {
        return std::nullopt;
    }
    if (IsPreview()) {
        return editor->PreviewHover(app, ToCanvas(hit));
    }
    if (kind_ == PanelKind::Dialog && editor->DialogHover(app, ToCanvas(hit))) {
        return std::nullopt;
    }
    return canvas_->OnHover(app, hit);
}

auto EditorPanel::OnLeft(AppState& app, size_t pointer) -> void {
    const auto editor = editor_.lock();
    if (!editor || !canvas_ || !editor->Allows(pointer)) {
        return;
    }
    if (IsPreview()) {
        editor->PreviewLeft(app);
        return;
    }
    canvas_->OnLeft(app, pointer);
}

auto EditorPanel::OnPointer(AppState& app, const PointerHit& hit, bool pressed) -> void {
    const auto editor = editor_.lock();
    if (!editor || !canvas_ || !editor->Allows(hit.pointer)) {
        return;
    }
    if (pressed) {
        editor->Claim(hit.pointer);
    }
    if (IsPreview()) {
        editor->PreviewPointer(app, kind_, hit, ToCanvas(hit), pressed);
    } else if (!(kind_ == PanelKind::Dialog && editor->DialogPointer(app, ToCanvas(hit), pressed))) {
        if (pressed) {
            editor->CommitText(app);
        } else {
            editor->PanelReleased(app, kind_, hit);
        }
        canvas_->OnPointer(app, hit, pressed);
    }
    if (!pressed) {
        editor->Unclaim(hit.pointer);
    }
}

auto EditorPanel::OnScroll(AppState& app, const PointerHit& hit, float delta_y, float delta_x) -> void {
    const auto editor = editor_.lock();
    if (!editor || !canvas_ || !editor->Allows(hit.pointer)) {
        return;
    }
    if (IsPreview()) {
        editor->PreviewScroll(app, ToCanvas(hit), delta_y);
        return;
    }
    canvas_->OnScroll(app, hit, delta_y, delta_x);
}
