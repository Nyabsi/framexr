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
#include <system_error>
#include <format>

#include "Config.hpp"
#include "Logger.hpp"
#include "Modular.hpp"
#include "State.hpp"
#include "Yaml.hpp"

namespace {

constexpr char kWatchYaml[] = {
#embed "watch.yaml"
    ,
    '\0'
};

} // namespace

auto Watch::Create(AppState& app) -> Result<std::unique_ptr<Overlay>> {
    const auto config = LoadConfig(app.config.watch_profile);

    OverlayState state;
    state.name = kWatchName;
    state.want_visible = true;
    state.interactable = true;
    state.z_order = kZOrderWatch;
    state.spawn_scale = config.width;
    state.spawn_point = app.config.watch_pos;
    state.spawn_rotation = app.config.watch_rot;
    const auto size = config.Size();
    state.interaction_transform = OverlayState::UiTransform({size[0], size[1]});
    state.relative_to = app.config.watch_hand == Handedness::Left ? RelativeTo::LeftHand : RelativeTo::RightHand;

    ASSIGN_OR_RETURN(auto canvas, Modular::CreateCanvas(config, app));
    return std::make_unique<Overlay>(std::move(state), std::move(canvas));
}

auto Watch::UserFile(std::string_view profile) -> std::filesystem::path {
    return Config::Root() / "watches" / std::format("{}.yaml", profile);
}

auto Watch::IsBuiltIn(std::string_view profile) -> bool {
    return profile == kDefaultProfile;
}

auto Watch::Profiles() -> std::vector<std::string> {
    std::vector<std::string> profiles{std::string(kDefaultProfile)};
    std::error_code error;
    std::vector<std::string> mine;
    for (const auto& entry : std::filesystem::directory_iterator(Config::Root() / "watches", error)) {
        if (entry.path().extension() == ".yaml" && entry.path().stem() != kDefaultProfile) {
            mine.push_back(entry.path().stem().string());
        }
    }
    std::ranges::sort(mine);
    profiles.insert(profiles.end(), mine.begin(), mine.end());
    return profiles;
}

auto Watch::LoadConfig(std::string_view profile) -> ModularUiConfig {
    if (const auto text = Config::ReadFile(UserFile(profile))) {
        if (auto parsed = Yaml::Parse(*text)) {
            try {
                return parsed->get<ModularUiConfig>();
            } catch (const nlohmann::json::exception& error) {
                LOG_ERROR("Watch layout {} is broken: {}", profile, error.what());
            }
        } else {
            LOG_ERROR("Watch layout {} is broken: {}", profile, parsed.error().Message());
        }
    } else if (!IsBuiltIn(profile)) {
        LOG_WARN("Unknown watch layout {}, using {}", profile, kDefaultProfile);
    }
    auto built_in = Yaml::Parse(kWatchYaml);
    if (!built_in) {
        FatalError(std::format("Built-in watch is broken: {}", built_in.error().Message()));
    }
    return built_in->get<ModularUiConfig>();
}

auto Watch::RebuildCanvas(AppState& app, Overlay& watch) -> Result<> {
    return Apply(app, watch, LoadConfig(app.config.watch_profile));
}

auto Watch::Apply(AppState& app, Overlay& watch, const ModularUiConfig& config) -> Result<> {
    ASSIGN_OR_RETURN(auto canvas, Modular::CreateCanvas(config, app));
    const auto size = config.Size();
    watch.backend = std::move(canvas);
    watch.state.interaction_transform = OverlayState::UiTransform({size[0], size[1]});
    watch.state.spawn_scale = config.width;
    watch.state.dirty = true;
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
