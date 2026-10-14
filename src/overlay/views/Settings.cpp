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

#include "Settings.hpp"

#include <format>

#include "Forms.hpp"
#include "ActionBinding.hpp"
#include "Compose.hpp"
#include "Form.hpp"
#include "Logger.hpp"

auto Settings::Create(std::string name) -> std::unique_ptr<Settings>
{
    auto loaded = Forms::Load(name);
    if (!loaded)
    {
        LOG_ERROR("Failed to load custom UI config for {}: {}", name, loaded.error().Message());
        return nullptr;
    }
    const auto& config = *loaded;

    auto canvas = Compose::Create(Form::FromDocument(config, name, ActionBinding::Bind));
    if (!canvas)
    {
        LOG_ERROR("Failed to create canvas for {}: {}", name, canvas.error().Message());
        return nullptr;
    }

    return std::unique_ptr<Settings>(new Settings(std::move(name), config, std::move(*canvas)));
}

Settings::Settings(std::string name, const FormDocument& config, std::unique_ptr<Compose> compose) : View(std::move(compose))
{
    const auto spawn = config.SpawnPos().value_or(std::array{0.0f, 0.0f, -0.5f});
    const auto size = config.Size();
    SetName(std::move(name));
    SetWantVisible(true);
    SetInteractable(true);
    SetGrabbable(true);
    SetSpawnScale(config.Width());
    SetSpawnPoint(Eigen::Vector3f(spawn[0], spawn[1], spawn[2]));
    SetInteractionTransform(View::UiTransform({size[0], size[1]}));
}
