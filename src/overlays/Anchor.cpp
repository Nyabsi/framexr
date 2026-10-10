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

#include "Anchor.hpp"

#include "Config.hpp"
#include "Modular.hpp"
#include "State.hpp"

namespace {

constexpr char kAnchorYaml[] = {
#embed "anchor.yaml"
    ,
    '\0'
};

} // namespace

auto Anchor::Create(AppState& app) -> Result<std::unique_ptr<Overlay>> {
    const auto config = Config::LoadKnownYaml<ModularUiConfig>("anchor.yaml", kAnchorYaml);

    OverlayState state;
    state.name = kAnchorName;
    state.want_visible = false;
    state.interactable = false;
    state.grabbable = false;
    state.z_order = kZOrderAnchor;
    state.spawn_scale = config.width;
    state.spawn_point = -Eigen::Vector3f::UnitZ() * 0.5f;
    state.relative_to = RelativeTo::Stage;

    ASSIGN_OR_RETURN(auto canvas, Modular::CreateCanvas(config.size, config.elements, app));
    return std::make_unique<Overlay>(std::move(state), std::move(canvas));
}
