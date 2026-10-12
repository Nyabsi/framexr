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

#include "KeyboardBackend.hpp"

#include <utility>

#include "Canvas.hpp"
#include "CommandBatch.hpp"
#include "InteractionHandler.hpp"
#include "Keyboard.hpp"
#include "KeyboardState.hpp"
#include "OverlayRenderer.hpp"
#include "framexr_config.h"

KeyboardBackend::KeyboardBackend(std::unique_ptr<Canvas> canvas, std::shared_ptr<KeyboardState> state)
    : m_canvas(std::move(canvas)),
      m_state(std::move(state)) {
}

auto KeyboardBackend::SetRenderer(std::unique_ptr<OverlayRenderer> renderer) -> void {
    m_canvas->SetRenderer(std::move(renderer));
}

auto KeyboardBackend::SetInteraction(std::unique_ptr<InteractionHandler> interaction) -> void {
    m_canvas->SetInteraction(std::move(interaction));
}

auto KeyboardBackend::Init() -> Result<> {
    return m_canvas->Init();
}

auto KeyboardBackend::Pause() -> Result<> {
    m_state->SetModifiers(0);
    m_state->RefreshLetterCase();
    Keyboard::SetModifiers(0);
    return m_canvas->Pause();
}

auto KeyboardBackend::Resume() -> Result<> {
    return m_canvas->Resume();
}

auto KeyboardBackend::ShouldRender() -> Result<RenderReadiness> {
    return m_canvas->ShouldRender();
}

auto KeyboardBackend::Render(const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> {
    return m_canvas->Render(target, batch, alpha);
}

auto KeyboardBackend::Meta() -> std::optional<FrameMeta> {
    return m_canvas->Meta();
}

auto KeyboardBackend::OnHover(const PointerHit& hit) -> std::optional<Haptics> {
    return m_canvas->OnHover(hit);
}

auto KeyboardBackend::OnLeft(size_t pointer) -> void {
    m_canvas->OnLeft(pointer);
}

auto KeyboardBackend::OnPointer(const PointerHit& hit, bool pressed) -> void {
    m_canvas->OnPointer(hit, pressed);
}

auto KeyboardBackend::OnScroll(const PointerHit& hit, float delta_y, float delta_x) -> void {
    m_canvas->OnScroll(hit, delta_y, delta_x);
}
