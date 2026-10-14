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
#include "Forms.hpp"
#include "Interaction.hpp"
#include "Logger.hpp"
#include "ActionBinding.hpp"
#include "Compose.hpp"
#include "Form.hpp"

Watch::Watch()
{
    SetName(std::string(kWatchName));
    SetWantVisible(true);
    SetInteractable(true);
    SetZOrder(kZOrderWatch);
    SetSpawnPoint(g_config->watch_pos);
    SetSpawnRotation(g_config->watch_rot);
    SetAttachedTo(g_config->watch_hand == Handedness::Left ? RelativeTo::LeftHand : RelativeTo::RightHand);
}

auto Watch::Create() -> Result<std::unique_ptr<Watch>>
{
    auto watch = std::unique_ptr<Watch>(new Watch());
    RETURN_IF_ERROR(watch->Rebuild());
    return watch;
}

auto Watch::Rebuild() -> Result<>
{
    return Apply(Forms::Watch(g_config->watch_profile));
}

auto Watch::Apply(const FormDocument& config) -> Result<>
{
    ASSIGN_OR_RETURN(auto canvas, Compose::Create(Form::FromDocument(config, std::string(kWatchName), ActionBinding::Bind)));
    const auto size = config.Size();
    SetComposer(std::move(canvas));
    SetInteractionTransform(View::UiTransform({size[0], size[1]}));
    SetSpawnScale(config.Width());
    SetDirty(true);
    return {};
}

auto Watch::Update() -> void
{
    if (SavedTransform())
    {
        SetWantVisible(false);
        return;
    }
    const Eigen::Vector3f to_hmd = (Transform().translation() - g_interaction->Hmd().translation()).normalized();
    const Eigen::Vector3f normal = (Transform().linear() * -Eigen::Vector3f::UnitZ()).normalized();
    const float dot = to_hmd.dot(normal);
    const auto& config = *g_config;
    SetWantVisible(dot >= config.watch_view_angle_min);
    if (WantVisible())
    {
        const float alpha = (dot - config.watch_view_angle_min) / (config.watch_view_angle_max - config.watch_view_angle_min)
                          + 0.1f;
        SetAlpha(std::clamp(alpha, 0.0f, 1.0f));
    }
}
