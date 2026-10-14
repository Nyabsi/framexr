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

#include "View.hpp"
#include "PanelPlacement.hpp"

class Editor;

class EditorPanel final : public View
{
  public:
    EditorPanel(const std::shared_ptr<Editor>& editor, PanelKind kind);

    auto Update() -> void override;
    auto OnHover(const PointerHit& hit) -> std::optional<Haptics> override;
    auto OnLeft(size_t pointer) -> void override;
    auto OnPointer(const PointerHit& hit, bool pressed) -> void override;
    auto OnScroll(const PointerHit& hit, float delta_y, float delta_x) -> void override;
    [[nodiscard]] auto Accepts(const Eigen::Vector2f& uv) const -> bool override;

  private:
    [[nodiscard]] auto IsPreview() const -> bool;
    [[nodiscard]] auto ToPanel(const PointerHit& hit) const -> Eigen::Vector2f;
    auto Rebuild(Editor& editor) -> void;

    std::weak_ptr<Editor> m_editor;
    PanelKind m_kind;
    uint64_t m_built = 0;
};
