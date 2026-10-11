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
#include "Editor.hpp"
#include "Overlay.hpp"

class EditorPanel final : public OverlayBackend {
  public:
    EditorPanel(std::weak_ptr<Editor> editor, PanelKind kind);

    auto SetRenderer(std::unique_ptr<OverlayRenderer> renderer) -> void override;
    auto SetInteraction(std::unique_ptr<InteractionHandler> interaction) -> void override;

    auto Init(AppState& app) -> Result<> override;
    auto Pause(AppState& app) -> Result<> override;
    auto Resume(AppState& app) -> Result<> override;
    auto ShouldRender(AppState& app) -> Result<RenderReadiness> override;
    auto Render(AppState& app, const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> override;
    auto Meta() -> std::optional<FrameMeta> override;

    auto OnHover(AppState& app, const PointerHit& hit) -> std::optional<Haptics> override;
    auto OnLeft(AppState& app, size_t pointer) -> void override;
    auto OnPointer(AppState& app, const PointerHit& hit, bool pressed) -> void override;
    auto OnScroll(AppState& app, const PointerHit& hit, float delta_y, float delta_x) -> void override;
    [[nodiscard]] auto Accepts(const Eigen::Vector2f& uv) const -> bool override;

    [[nodiscard]] auto CanvasForTest() -> Canvas* { return canvas_.get(); }

  private:
    auto Refresh(AppState& app) -> Result<>;
    [[nodiscard]] auto ToCanvas(const PointerHit& hit) const -> Eigen::Vector2f;
    [[nodiscard]] auto IsPreview() const -> bool;

    std::weak_ptr<Editor> editor_;
    PanelKind kind_;
    std::unique_ptr<Canvas> canvas_;
    uint64_t built_ = 0;
};
