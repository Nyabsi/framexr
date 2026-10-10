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

#include "CustomUi.hpp"

#include <format>

#include "Config.hpp"
#include "Logger.hpp"
#include "Modular.hpp"
#include "State.hpp"
#include "Yaml.hpp"

namespace {

constexpr std::string_view kSettingsName = "settings";

constexpr char kSettingsYaml[] = {
#embed "settings.yaml"
    ,
    '\0'
};

} // namespace

auto CustomUi::Load(const std::string& name) -> Result<ModularUiConfig> {
    const auto filename = std::format("{}.yaml", name);
    const auto text = Config::LoadText(filename);
    if (!text) {
        return Unexpected(std::format("Could not read file at {}", filename));
    }
    ASSIGN_OR_RETURN(const auto json, Yaml::Parse(*text));
    try {
        return json.get<ModularUiConfig>();
    } catch (const nlohmann::json::exception& error) {
        return Unexpected(error.what());
    }
}

auto CustomUi::Create(AppState& app, std::string name) -> std::optional<std::pair<OverlayState, std::unique_ptr<OverlayBackend>>> {
    ModularUiConfig config;
    if (name == kSettingsName) {
        config = Config::LoadKnownYaml<ModularUiConfig>("settings.yaml", kSettingsYaml);
    } else if (auto loaded = Load(name)) {
        config = std::move(*loaded);
    } else {
        LOG_ERROR("Failed to load custom UI config for {}: {}", name, loaded.error().Message());
        return std::nullopt;
    }

    auto canvas = Modular::CreateCanvas(config.size, config.elements, app);
    if (!canvas) {
        LOG_ERROR("Failed to create canvas for {}: {}", name, canvas.error().Message());
        return std::nullopt;
    }

    const auto spawn = config.spawn_pos.value_or(std::array{0.0f, 0.0f, -0.5f});
    OverlayState state;
    state.name = std::move(name);
    state.want_visible = true;
    state.interactable = true;
    state.grabbable = true;
    state.spawn_scale = config.width;
    state.spawn_point = Eigen::Vector3f(spawn[0], spawn[1], spawn[2]);
    state.interaction_transform = OverlayState::UiTransform({config.size[0], config.size[1]});
    return std::pair<OverlayState, std::unique_ptr<OverlayBackend>>{std::move(state), std::move(*canvas)};
}
