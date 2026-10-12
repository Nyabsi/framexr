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
#include "Overlay.hpp"
#include "Task.hpp"
#include "framexr_config.h"

class Editor;
class Keymap;
class WaylandClient;
struct ScreenOverlay;

struct ScreenMeta {
    std::string name;
    OverlayId id{};
    uint32_t native_handle = 0;
};

struct EditorLine {
    Eigen::Affine3f pose = Eigen::Affine3f::Identity();
    float length = 0.0f;
};

class OverlayContainer final {
  public:
    static auto Create() -> Result<std::unique_ptr<OverlayContainer>>;
    static auto CreateEmpty() -> std::unique_ptr<OverlayContainer>;

    ~OverlayContainer();
    OverlayContainer(const OverlayContainer&) = delete;
    auto operator=(const OverlayContainer&) -> OverlayContainer& = delete;

    [[nodiscard]] auto Overlays() -> std::vector<std::unique_ptr<Overlay>>& { return m_overlays; }
    [[nodiscard]] auto Overlays() const -> const std::vector<std::unique_ptr<Overlay>>& { return m_overlays; }

    auto ById(OverlayId id) -> Overlay*;
    auto ByName(std::string_view name) -> Overlay*;
    auto BySelector(const OverlaySelector& selector) -> Overlay*;

    auto Add(std::unique_ptr<Overlay> overlay) -> void;
    auto Remove(const OverlaySelector& selector) -> std::unique_ptr<Overlay>;
    auto Update() -> Result<std::vector<std::unique_ptr<Overlay>>>;
    auto ShowHide() -> void;
    auto Recenter() -> void;
    auto RememberUserPlacement(Overlay& placed) -> void;
    auto PlaceShown(Overlay& shown) -> void;
    auto UpdateTexelFootprints() -> void;
    auto OpenEditor() -> void;
    auto UpdateEditor() -> void;
    [[nodiscard]] auto EditorLines() -> std::vector<EditorLine>;
    [[nodiscard]] auto ActiveEditor() -> Editor* { return m_editor.get(); }
    [[nodiscard]] auto Screens() -> std::vector<ScreenMeta>& { return m_screens; }

  private:
    OverlayContainer();

    using OverlayPair = std::pair<OverlayId, OverlayId>;

    auto LockedVisible() -> std::vector<Overlay*>;
    auto FreeVisible() -> std::vector<Overlay*>;
    auto ArrangeVisible() -> void;

    auto RemoveAt(size_t index) -> std::unique_ptr<Overlay>;
    auto CreateScreens(std::optional<Keymap>& keymap) -> Result<std::vector<ScreenOverlay>>;

    std::vector<std::unique_ptr<Overlay>> m_overlays;
    std::set<OverlayPair> m_user_overlaps;
    std::vector<OverlayId> m_hidden_by_show_hide;
    std::shared_ptr<Editor> m_editor;
    std::vector<ScreenMeta> m_screens;
#if FRAMEXR_FEATURE_WAYLAND
    std::unique_ptr<WaylandClient> m_wayland;
#endif
};

extern OverlayContainer* g_overlays;
