// FrameXR
//
// This is a port of wlx-overlay-s from Rust to C++.
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
// Copyright (C) 2023-2025 galister and wlx-overlay-s contributors
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

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Error.hpp"
#include "View.hpp"
#include "TaskQueue.hpp"
#include "framexr_config.h"

class Editor;
class Keymap;
class WaylandClient;
class Desktop;

struct ViewRect {
    float min_azimuth = 0.0f;
    float max_azimuth = 0.0f;
    float min_elevation = 0.0f;
    float max_elevation = 0.0f;
};

struct EditorLine {
    Eigen::Affine3f pose = Eigen::Affine3f::Identity();
    float length = 0.0f;
};

class Container final {
  public:
    static auto Create() -> Result<std::unique_ptr<Container>>;
    static auto CreateEmpty() -> std::unique_ptr<Container>;
    static auto ArcPerChord(const std::optional<float>& curvature) -> float;
    static auto Corners(View& view) -> std::optional<std::array<Eigen::Vector3f, 4>>;
    static auto ViewBounds(const std::array<Eigen::Vector3f, 4>& corners, const Eigen::Affine3f& head) -> ViewRect;
    static auto Overlaps(const ViewRect& a, const ViewRect& b, float gap) -> bool;
    static auto MeasureFootprint(View& view, const Eigen::Affine3f& head, const Eigen::Vector2f& display_pixels_per_radian)
        -> std::optional<TexelFootprint>;
    static auto NudgeClear(View& moving, std::span<View* const> fixed, const Eigen::Affine3f& head) -> bool;

    ~Container();
    Container(const Container&) = delete;
    auto operator=(const Container&) -> Container& = delete;

    [[nodiscard]] auto Views() -> std::vector<std::unique_ptr<View>>& { return m_views; }
    [[nodiscard]] auto Views() const -> const std::vector<std::unique_ptr<View>>& { return m_views; }
    [[nodiscard]] auto Desktops() const -> std::vector<Desktop*>;

    auto ById(OverlayId id) -> View*;
    auto ByName(std::string_view name) -> View*;
    auto BySelector(const OverlaySelector& selector) -> View*;

    auto Add(std::unique_ptr<View> overlay) -> void;
    auto Remove(const OverlaySelector& selector) -> std::unique_ptr<View>;
    auto Update() -> Result<std::vector<std::unique_ptr<View>>>;
    auto ShowHide() -> void;
    auto Recenter() -> void;
    auto RememberUserPlacement(View& placed) -> void;
    auto PlaceShown(View& shown) -> void;
    auto UpdateTexelFootprints() -> void;
    auto OpenEditor() -> void;
    auto UpdateEditor() -> void;
    [[nodiscard]] auto EditorLines() -> std::vector<EditorLine>;
    [[nodiscard]] auto ActiveEditor() -> Editor* { return m_editor.get(); }

  private:
    Container();

    using OverlayPair = std::pair<OverlayId, OverlayId>;

    auto LockedVisible() -> std::vector<View*>;
    auto FreeVisible() -> std::vector<View*>;
    auto ArrangeVisible() -> void;

    auto RemoveAt(size_t index) -> std::unique_ptr<View>;
    auto CreateDesktops(std::optional<Keymap>& keymap) -> Result<std::vector<std::unique_ptr<Desktop>>>;
    static auto RotateAroundHead(View& view, const Eigen::Affine3f& head, const Eigen::Vector3f& axis, float angle) -> void;

    std::vector<std::unique_ptr<View>> m_views;
    std::set<OverlayPair> m_user_overlaps;
    std::vector<OverlayId> m_hidden_by_show_hide;
    std::shared_ptr<Editor> m_editor;
#if FRAMEXR_FEATURE_WAYLAND
    std::unique_ptr<WaylandClient> m_wayland;
#endif
};

extern Container* g_container;
