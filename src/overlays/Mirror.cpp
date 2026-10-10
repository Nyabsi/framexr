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
#include "Logger.hpp"
#include "PipewireCapture.hpp"
#include "State.hpp"

MirrorRenderer::MirrorRenderer(std::string name) : name_(std::move(name)), selection_(ScreenCastPortal::SelectScreen({})) {
}

auto MirrorRenderer::Create(std::string name, bool show_hide, const Config& config)
    -> std::pair<OverlayState, std::unique_ptr<OverlayBackend>> {
    OverlayState state;
    state.name = name;
    state.show_hide = show_hide;
    state.want_visible = true;
    state.spawn_scale = 0.5f * config.desktop_view_scale;

    auto backend = std::make_unique<SplitOverlayBackend>();
    backend->SetRenderer(std::make_unique<MirrorRenderer>(std::move(name)));
    return {std::move(state), std::move(backend)};
}

auto MirrorRenderer::Init(AppState&) -> Result<> {
    return {};
}

auto MirrorRenderer::Pause(AppState& app) -> Result<> {
    if (renderer_) {
        RETURN_IF_ERROR(renderer_->Pause(app));
    }
    return {};
}

auto MirrorRenderer::Resume(AppState& app) -> Result<> {
    if (renderer_) {
        RETURN_IF_ERROR(renderer_->Resume(app));
    }
    return {};
}

auto MirrorRenderer::ShouldRender(AppState& app) -> Result<RenderReadiness> {
    if (!renderer_) {
        return RenderReadiness::Unable;
    }
    return renderer_->ShouldRender(app);
}

auto MirrorRenderer::Render(AppState& app, const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> {
    if (selection_) {
        if (selection_->wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return false;
        }
        auto selection = selection_->get();
        selection_.reset();

        if (selection && !selection->streams.empty()) {
            const uint32_t node_id = selection->streams.front().node_id;
            LOG_INFO("{}: PipeWire node selected: {}", name_, node_id);
            renderer_ = std::make_unique<ScreenRenderer>(name_, std::make_unique<PipewireCapture>(name_, node_id));
            app.tasks.Enqueue(
                ModifyOverlayTask{
                    .selector = name_,
                    .modify = [](AppState& state, OverlayState& overlay) -> void {
                        overlay.grabbable = true;
                        overlay.interactable = true;
                        overlay.Reset(state, false);
                    },
                }
            );
        } else {
            LOG_WARN(
                "Failed to create mirror due to PipeWire error: {}",
                selection ? std::string("no streams selected") : selection.error().Message()
            );
            renderer_.reset();
            app.tasks.Enqueue(DropOverlayTask{.selector = name_});
        }
    }

    if (!renderer_) {
        return false;
    }

    ASSIGN_OR_RETURN(const bool rendered, renderer_->Render(app, target, batch, alpha));
    if (const auto meta = renderer_->Meta()) {
        const VkExtent2D extent = meta->extent;
        if (extent.width != last_extent_.width || extent.height != last_extent_.height) {
            last_extent_ = extent;
            app.tasks.Enqueue(
                ModifyOverlayTask{
                    .selector = name_,
                    .modify = [extent](AppState&, OverlayState& overlay) -> void {
                        overlay.interaction_transform = OverlayState::UiTransform(extent);
                    },
                }
            );
        }
    }
    return rendered;
}

auto MirrorRenderer::Meta() -> std::optional<FrameMeta> {
    return FrameMeta{.extent = last_extent_};
}
