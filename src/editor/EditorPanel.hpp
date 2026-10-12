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

#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "Canvas.hpp"
#include "CommandBatch.hpp"
#include "Editor.hpp"
#include "InteractionHandler.hpp"
#include "Overlay.hpp"
#include "OverlayRenderer.hpp"

class EditorPanel final : public OverlayBackend {
  public:
    EditorPanel(std::weak_ptr<Editor> editor, PanelKind kind);

    auto SetRenderer(std::unique_ptr<OverlayRenderer> renderer) -> void override;
    auto SetInteraction(std::unique_ptr<InteractionHandler> interaction) -> void override;

    auto Init() -> Result<> override;
    auto Pause() -> Result<> override;
    auto Resume() -> Result<> override;
    auto ShouldRender() -> Result<RenderReadiness> override;
    auto Render(const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> override;
    auto Meta() -> std::optional<FrameMeta> override;

    auto OnHover(const PointerHit& hit) -> std::optional<Haptics> override;
    auto OnLeft(size_t pointer) -> void override;
    auto OnPointer(const PointerHit& hit, bool pressed) -> void override;
    auto OnScroll(const PointerHit& hit, float delta_y, float delta_x) -> void override;
    [[nodiscard]] auto Accepts(const Eigen::Vector2f& uv) const -> bool override;

    [[nodiscard]] auto CanvasForTest() -> Canvas* { return m_canvas.get(); }

  private:
    auto Refresh() -> Result<>;
    [[nodiscard]] auto ToCanvas(const PointerHit& hit) const -> Eigen::Vector2f;
    [[nodiscard]] auto IsPreview() const -> bool;

    std::weak_ptr<Editor> m_editor;
    PanelKind m_kind;
    std::unique_ptr<Canvas> m_canvas;
    uint64_t m_built = 0;
};
