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

#include "Mirror.hpp"

#include <chrono>

#include "Config.hpp"
#include "Compose.hpp"
#include "Logger.hpp"
#include "PipewireCapture.hpp"
#include "ScreenRenderer.hpp"
#include "TaskQueue.hpp"

Mirror::Mirror(std::string name, bool show_hide) : m_selection(ScreenCastPortal::SelectScreen({})) {
    SetName(std::move(name));
    SetShowHide(show_hide);
    SetWantVisible(true);
    SetSpawnScale(0.5f * g_config->desktop_view_scale);
}

auto Mirror::Update() -> void {
    if (m_selection && m_selection->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        Attach();
    }
    if (const auto meta = Composer()->Meta();
        meta && (meta->extent.width != m_extent.width || meta->extent.height != m_extent.height)) {
        m_extent = meta->extent;
        SetInteractionTransform(UiTransform(m_extent));
    }
}

auto Mirror::Attach() -> void {
    auto selection = m_selection->get();
    m_selection.reset();
    if (!selection || selection->streams.empty()) {
        LOG_WARN(
            "Failed to create mirror due to PipeWire error: {}",
            selection ? std::string("no streams selected") : selection.error().Message()
        );
        g_tasks->Enqueue(DropOverlayTask{.selector = Name()});
        return;
    }
    const uint32_t node_id = selection->streams.front().node_id;
    LOG_INFO("{}: PipeWire node selected: {}", Name(), node_id);
    SetComposer(
        std::make_unique<Compose>(std::make_unique<ScreenRenderer>(Name(), std::make_unique<PipewireCapture>(Name(), node_id)))
    );
    SetGrabbable(true);
    SetInteractable(true);
    Reset(false);
}
