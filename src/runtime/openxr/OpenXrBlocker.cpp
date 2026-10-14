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

#include "OpenXrBlocker.hpp"

#include <algorithm>

#include "Config.hpp"
#include "Interaction.hpp"
#include "Logger.hpp"
#include "Monado.hpp"

auto OpenXrBlocker::Update(OverlayId watch_id, Monado& monado) -> void
{
    if (!g_config->block_game_input)
    {
        return;
    }

    const bool any_hovered = std::ranges::any_of(
        g_interaction->Pointers(),
        [&](const LaserPointer& pointer) -> bool
        {
            const auto& hovered = pointer.Interaction().hovered_id;
            return hovered && (*hovered != watch_id || !g_config->block_game_input_ignore_watch);
        }
    );

    if (any_hovered && !m_hovered_last_frame)
    {
        LOG_INFO("Blocking input");
        SetClientsIoActive(monado, false);
    }
    else if (!any_hovered && m_hovered_last_frame)
    {
        LOG_INFO("Unblocking input");
        SetClientsIoActive(monado, true);
    }
    m_hovered_last_frame = any_hovered;
}

auto OpenXrBlocker::SetClientsIoActive(Monado& monado, bool active) -> void
{
    const auto clients = monado.Clients();
    if (!clients)
    {
        LOG_WARN("Failed to get clients from Monado: {}", Monado::ResultName(clients.error()));
        return;
    }
    for (const auto client : *clients)
    {
        const auto name = monado.ClientName(client);
        if (!name)
        {
            LOG_WARN("Failed to get client name: {}", Monado::ResultName(name.error()));
            continue;
        }
        const auto state = monado.ClientState(client);
        if (!state)
        {
            LOG_WARN("Failed to get client state: {}", Monado::ResultName(state.error()));
            continue;
        }
        if (*name != "framexr" && (*state & kMonadoClientSessionVisible) != 0)
        {
            if (const auto result = monado.SetClientIoActive(client, active); !result)
            {
                LOG_WARN("Failed to set io active for client: {}", Monado::ResultName(result.error()));
            }
        }
    }
}
