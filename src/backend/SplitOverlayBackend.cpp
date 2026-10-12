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

#include "SplitOverlayBackend.hpp"

#include "CommandBatch.hpp"
#include "CommandBuffer.hpp"
#include "Image.hpp"
#include "InteractionHandler.hpp"
#include "NullInteraction.hpp"
#include "NullRenderer.hpp"
#include "OverlayRenderer.hpp"

SplitOverlayBackend::SplitOverlayBackend()
    : m_renderer(std::make_unique<NullRenderer>()),
      m_interaction(std::make_unique<NullInteraction>()) {
}

SplitOverlayBackend::SplitOverlayBackend(std::unique_ptr<OverlayRenderer> renderer, std::unique_ptr<InteractionHandler> interaction)
    : m_renderer(renderer ? std::move(renderer) : std::make_unique<NullRenderer>()),
      m_interaction(interaction ? std::move(interaction) : std::make_unique<NullInteraction>()) {
}

auto SplitOverlayBackend::SetRenderer(std::unique_ptr<OverlayRenderer> renderer) -> void {
    m_renderer = std::move(renderer);
}

auto SplitOverlayBackend::SetInteraction(std::unique_ptr<InteractionHandler> interaction) -> void {
    m_interaction = std::move(interaction);
}

auto SplitOverlayBackend::Init() -> Result<> {
    return m_renderer->Init();
}

auto SplitOverlayBackend::Pause() -> Result<> {
    return m_renderer->Pause();
}

auto SplitOverlayBackend::Resume() -> Result<> {
    return m_renderer->Resume();
}

auto SplitOverlayBackend::ShouldRender() -> Result<RenderReadiness> {
    return m_renderer->ShouldRender();
}

auto SplitOverlayBackend::Render(const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> {
    return m_renderer->Render(target, batch, alpha);
}

auto SplitOverlayBackend::Meta() -> std::optional<FrameMeta> {
    return m_renderer->Meta();
}

auto SplitOverlayBackend::SetTexelFootprint(const TexelFootprint& footprint) -> void {
    m_renderer->SetTexelFootprint(footprint);
}

auto SplitOverlayBackend::OnHover(const PointerHit& hit) -> std::optional<Haptics> {
    return m_interaction->OnHover(hit);
}

auto SplitOverlayBackend::OnLeft(size_t pointer) -> void {
    m_interaction->OnLeft(pointer);
}

auto SplitOverlayBackend::OnPointer(const PointerHit& hit, bool pressed) -> void {
    m_interaction->OnPointer(hit, pressed);
}

auto SplitOverlayBackend::OnScroll(const PointerHit& hit, float delta_y, float delta_x) -> void {
    m_interaction->OnScroll(hit, delta_y, delta_x);
}

auto SplitOverlayBackend::Accepts(const Eigen::Vector2f& uv) const -> bool {
    return m_interaction->Accepts(uv);
}
