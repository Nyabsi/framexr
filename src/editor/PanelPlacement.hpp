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

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <Eigen/Geometry>

#include "Container.hpp"
#include "FormDesigner.hpp"

class Editor;

enum class PanelKind : uint8_t {
    Preview,
    Toolbar,
    Palette,
    Properties,
    Dialog,
    Ghost,
    BackLayer,
    MiddleLayer,
    FrontLayer,
};

class PanelPlacement final {
  public:
    static constexpr std::array<PanelKind, 9> kPanels{
        PanelKind::Preview,
        PanelKind::Toolbar,
        PanelKind::Palette,
        PanelKind::Properties,
        PanelKind::Dialog,
        PanelKind::Ghost,
        PanelKind::BackLayer,
        PanelKind::MiddleLayer,
        PanelKind::FrontLayer,
    };
    static constexpr float kMetersPerPixel = 0.0009f;
    static constexpr float kRadius = 0.75f;
    static constexpr float kLayerSpacing = 0.07f;

    static auto PanelName(PanelKind kind) -> std::string_view;
    static auto LayerOf(PanelKind kind) -> std::optional<int>;
    static auto LocalTransform(const Editor& editor, PanelKind kind) -> Eigen::Affine3f;

    [[nodiscard]] auto Anchor() const -> const Eigen::Affine3f& { return m_anchor; }
    [[nodiscard]] auto PaletteOnHand() const -> bool { return m_palette_on_hand; }
    [[nodiscard]] auto ShowsPreview(OverlayId id) const -> bool;
    [[nodiscard]] auto ShowsPalette(OverlayId id) const -> bool { return m_palette_id == id; }

    auto SetPaletteOnHand(bool on_hand) -> void { m_palette_on_hand = on_hand; }
    auto Recenter() -> void;
    auto Open(const std::shared_ptr<Editor>& editor) -> void;
    auto Close() -> void;
    auto Place(const Editor& editor) -> void;
    [[nodiscard]] auto Lines(const Editor& editor) const -> std::vector<EditorLine>;

  private:
    Eigen::Affine3f m_anchor = Eigen::Affine3f::Identity();
    bool m_palette_on_hand = false;
    std::optional<OverlayId> m_preview_id;
    std::array<std::optional<OverlayId>, FormDesigner::kLayers> m_layer_ids;
    std::optional<OverlayId> m_palette_id;
};
