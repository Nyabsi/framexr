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

#include "CommandBatch.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include "OverlayRenderer.hpp"
#include "PipewireCapture.hpp"
#include "ScreenRenderer.hpp"
#include "SplitOverlayBackend.hpp"
#include "Task.hpp"

MirrorRenderer::MirrorRenderer(std::string name) : m_name(std::move(name)), m_selection(ScreenCastPortal::SelectScreen({})) {
}

auto MirrorRenderer::Create(std::string name, bool show_hide) -> std::pair<OverlayState, std::unique_ptr<OverlayBackend>> {
    OverlayState state;
    state.SetName(name);
    state.SetShowHide(show_hide);
    state.SetWantVisible(true);
    state.SetSpawnScale(0.5f * g_config->desktop_view_scale);

    auto backend = std::make_unique<SplitOverlayBackend>();
    backend->SetRenderer(std::make_unique<MirrorRenderer>(std::move(name)));
    return {std::move(state), std::move(backend)};
}

auto MirrorRenderer::Init() -> Result<> {
    return {};
}

auto MirrorRenderer::Pause() -> Result<> {
    if (m_renderer) {
        RETURN_IF_ERROR(m_renderer->Pause());
    }
    return {};
}

auto MirrorRenderer::Resume() -> Result<> {
    if (m_renderer) {
        RETURN_IF_ERROR(m_renderer->Resume());
    }
    return {};
}

auto MirrorRenderer::ShouldRender() -> Result<RenderReadiness> {
    if (!m_renderer) {
        return RenderReadiness::Unable;
    }
    return m_renderer->ShouldRender();
}

auto MirrorRenderer::Render(const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> {
    if (m_selection) {
        if (m_selection->wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return false;
        }
        auto selection = m_selection->get();
        m_selection.reset();

        if (selection && !selection->streams.empty()) {
            const uint32_t node_id = selection->streams.front().node_id;
            LOG_INFO("{}: PipeWire node selected: {}", m_name, node_id);
            m_renderer = std::make_unique<ScreenRenderer>(m_name, std::make_unique<PipewireCapture>(m_name, node_id));
            g_tasks->Enqueue(
                ModifyOverlayTask{
                    .selector = m_name,
                    .modify = [](OverlayState& overlay) -> void {
                        overlay.SetGrabbable(true);
                        overlay.SetInteractable(true);
                        overlay.Reset(false);
                    },
                }
            );
        } else {
            LOG_WARN(
                "Failed to create mirror due to PipeWire error: {}",
                selection ? std::string("no streams selected") : selection.error().Message()
            );
            m_renderer.reset();
            g_tasks->Enqueue(DropOverlayTask{.selector = m_name});
        }
    }

    if (!m_renderer) {
        return false;
    }

    ASSIGN_OR_RETURN(const bool rendered, m_renderer->Render(target, batch, alpha));
    if (const auto meta = m_renderer->Meta()) {
        const VkExtent2D extent = meta->extent;
        if (extent.width != m_last_extent.width || extent.height != m_last_extent.height) {
            m_last_extent = extent;
            g_tasks->Enqueue(
                ModifyOverlayTask{
                    .selector = m_name,
                    .modify = [extent](OverlayState& overlay) -> void {
                        overlay.SetInteractionTransform(OverlayState::UiTransform(extent));
                    },
                }
            );
        }
    }
    return rendered;
}

auto MirrorRenderer::Meta() -> std::optional<FrameMeta> {
    return FrameMeta{.extent = m_last_extent};
}
