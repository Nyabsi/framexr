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
#include <string_view>
#include <utility>
#include <vector>

#include "Error.hpp"
#include "Overlay.hpp"
#include "Task.hpp"
#include "framexr_config.h"

class AppState;
class Editor;
class Keymap;
class WaylandClient;
struct ScreenOverlay;

struct EditorLine {
    Eigen::Affine3f pose = Eigen::Affine3f::Identity();
    float length = 0.0f;
};

class OverlayContainer final {
  public:
    static auto Create(AppState& app) -> Result<std::unique_ptr<OverlayContainer>>;
    static auto CreateEmpty() -> std::unique_ptr<OverlayContainer>;

    ~OverlayContainer();
    OverlayContainer(const OverlayContainer&) = delete;
    auto operator=(const OverlayContainer&) -> OverlayContainer& = delete;

    [[nodiscard]] auto Overlays() -> std::vector<std::unique_ptr<Overlay>>& { return overlays_; }
    [[nodiscard]] auto Overlays() const -> const std::vector<std::unique_ptr<Overlay>>& { return overlays_; }

    auto ById(OverlayId id) -> Overlay*;
    auto ByName(std::string_view name) -> Overlay*;
    auto BySelector(const OverlaySelector& selector) -> Overlay*;

    auto Add(std::unique_ptr<Overlay> overlay) -> void;
    auto Remove(const OverlaySelector& selector) -> std::unique_ptr<Overlay>;
    auto Update(AppState& app) -> Result<std::vector<std::unique_ptr<Overlay>>>;
    auto ShowHide(AppState& app) -> void;
    auto Recenter(AppState& app) -> void;
    auto RememberUserPlacement(const AppState& app, Overlay& placed) -> void;
    auto PlaceShown(const AppState& app, Overlay& shown) -> void;
    auto UpdateTexelFootprints(const AppState& app) -> void;
    auto OpenEditor(AppState& app) -> void;
    auto UpdateEditor(AppState& app) -> void;
    [[nodiscard]] auto EditorLines() -> std::vector<EditorLine>;
    [[nodiscard]] auto ActiveEditor() -> Editor* { return editor_.get(); }

  private:
    OverlayContainer();

    using OverlayPair = std::pair<OverlayId, OverlayId>;

    auto LockedVisible() -> std::vector<Overlay*>;
    auto FreeVisible(const AppState& app) -> std::vector<Overlay*>;
    auto ArrangeVisible(const AppState& app) -> void;

    auto RemoveAt(size_t index) -> std::unique_ptr<Overlay>;
    auto CreateScreens(AppState& app, std::optional<Keymap>& keymap) -> Result<std::vector<ScreenOverlay>>;

    std::vector<std::unique_ptr<Overlay>> overlays_;
    std::set<OverlayPair> user_overlaps_;
    std::vector<OverlayId> hidden_by_show_hide_;
    std::shared_ptr<Editor> editor_;
#if FRAMEXR_FEATURE_WAYLAND
    std::unique_ptr<WaylandClient> wayland_;
#endif
};
