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
#include <format>
#include <system_error>

#include "Config.hpp"
#include "Input.hpp"
#include "Logger.hpp"
#include "Modular.hpp"
#include "Yaml.hpp"

namespace {

constexpr char kWatchYaml[] = {
#embed "watch.yaml"
    ,
    '\0'
};

} // namespace

auto Watch::Create() -> Result<std::unique_ptr<Overlay>> {
    const auto config = LoadConfig(g_config->watch_profile);

    OverlayState state;
    state.SetName(std::string(kWatchName));
    state.SetWantVisible(true);
    state.SetInteractable(true);
    state.SetZOrder(kZOrderWatch);
    state.SetSpawnScale(config.Width());
    state.SetSpawnPoint(g_config->watch_pos);
    state.SetSpawnRotation(g_config->watch_rot);
    const auto size = config.Size();
    state.SetInteractionTransform(OverlayState::UiTransform({size[0], size[1]}));
    state.SetAttachedTo(g_config->watch_hand == Handedness::Left ? RelativeTo::LeftHand : RelativeTo::RightHand);

    ASSIGN_OR_RETURN(auto canvas, Modular::CreateCanvas(config));
    return std::make_unique<Overlay>(std::move(state), std::move(canvas));
}

auto Watch::UserFile(std::string_view profile) -> std::filesystem::path {
    return ConfigStore::Root() / "watches" / std::format("{}.yaml", profile);
}

auto Watch::IsBuiltIn(std::string_view profile) -> bool {
    return profile == kDefaultProfile;
}

auto Watch::Profiles() -> std::vector<std::string> {
    std::vector<std::string> profiles{std::string(kDefaultProfile)};
    std::error_code error;
    std::vector<std::string> mine;
    for (const auto& entry : std::filesystem::directory_iterator(ConfigStore::Root() / "watches", error)) {
        if (entry.path().extension() == ".yaml" && entry.path().stem() != kDefaultProfile) {
            mine.push_back(entry.path().stem().string());
        }
    }
    std::ranges::sort(mine);
    profiles.insert(profiles.end(), mine.begin(), mine.end());
    return profiles;
}

auto Watch::LoadConfig(std::string_view profile) -> ModularUiConfig {
    if (const auto text = ConfigStore::ReadFile(UserFile(profile))) {
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

auto Watch::RebuildCanvas(Overlay& watch) -> Result<> {
    return Apply(watch, LoadConfig(g_config->watch_profile));
}

auto Watch::Apply(Overlay& watch, const ModularUiConfig& config) -> Result<> {
    ASSIGN_OR_RETURN(auto canvas, Modular::CreateCanvas(config));
    const auto size = config.Size();
    watch.SetBackend(std::move(canvas));
    watch.State().SetInteractionTransform(OverlayState::UiTransform({size[0], size[1]}));
    watch.State().SetSpawnScale(config.Width());
    watch.State().SetDirty(true);
    return {};
}

auto Watch::Fade(Overlay& watch) -> void {
    auto& state = watch.State();
    if (state.SavedTransform()) {
        state.SetWantVisible(false);
        return;
    }

    const Eigen::Vector3f to_hmd = (state.Transform().translation() - g_input->Hmd().translation()).normalized();
    const Eigen::Vector3f watch_normal = (state.Transform().linear() * -Eigen::Vector3f::UnitZ()).normalized();
    const float dot = to_hmd.dot(watch_normal);

    const auto& config = *g_config;
    if (dot < config.watch_view_angle_min) {
        state.SetWantVisible(false);
    } else {
        state.SetWantVisible(true);
        const float alpha = (dot - config.watch_view_angle_min) / (config.watch_view_angle_max - config.watch_view_angle_min)
                          + 0.1f;
        state.SetAlpha(std::clamp(alpha, 0.0f, 1.0f));
    }
}
