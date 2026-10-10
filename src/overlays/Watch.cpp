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

#include "Watch.hpp"

#include <algorithm>

#include "Config.hpp"
#include "Logger.hpp"
#include "Modular.hpp"
#include "State.hpp"

namespace {

constexpr char kWatchYaml[] = {
#embed "watch.yaml"
    ,
    '\0'
};

auto LoadConfig() -> ModularUiConfig {
    return Config::LoadKnownYaml<ModularUiConfig>("watch.yaml", kWatchYaml);
}

} // namespace

auto Watch::Create(AppState& app) -> Result<std::unique_ptr<Overlay>> {
    const auto config = LoadConfig();

    OverlayState state;
    state.name = kWatchName;
    state.want_visible = true;
    state.interactable = true;
    state.z_order = kZOrderWatch;
    state.spawn_scale = config.width;
    state.spawn_point = app.config.watch_pos;
    state.spawn_rotation = app.config.watch_rot;
    state.interaction_transform = OverlayState::UiTransform({config.size[0], config.size[1]});
    state.relative_to = app.config.watch_hand == Handedness::Left ? RelativeTo::LeftHand : RelativeTo::RightHand;

    ASSIGN_OR_RETURN(auto canvas, Modular::CreateCanvas(config.size, config.elements, app));
    return std::make_unique<Overlay>(std::move(state), std::move(canvas));
}

auto Watch::RebuildCanvas(AppState& app, Overlay& watch) -> Result<> {
    const auto config = LoadConfig();
    ASSIGN_OR_RETURN(auto canvas, Modular::CreateCanvas(config.size, config.elements, app));
    watch.backend = std::move(canvas);
    return {};
}

auto Watch::Fade(AppState& app, Overlay& watch) -> void {
    auto& state = watch.state;
    if (state.saved_transform) {
        state.want_visible = false;
        return;
    }

    const Eigen::Vector3f to_hmd = (state.transform.translation() - app.input.hmd.translation()).normalized();
    const Eigen::Vector3f watch_normal = (state.transform.linear() * -Eigen::Vector3f::UnitZ()).normalized();
    const float dot = to_hmd.dot(watch_normal);

    const auto& config = app.config;
    if (dot < config.watch_view_angle_min) {
        state.want_visible = false;
    } else {
        state.want_visible = true;
        state.alpha = (dot - config.watch_view_angle_min) / (config.watch_view_angle_max - config.watch_view_angle_min);
        state.alpha += 0.1f;
        state.alpha = std::clamp(state.alpha, 0.0f, 1.0f);
    }
}
