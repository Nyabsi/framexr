// FrameXR
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
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

#include "RecordingBackend.hpp"

#include <utility>

#include "CommandBatch.hpp"
#include "InteractionHandler.hpp"
#include "OverlayRenderer.hpp"

RecordingBackend::RecordingBackend(VkExtent2D extent, std::shared_ptr<std::vector<RecordedEvent>> events)
    : m_extent(extent),
      m_events(std::move(events)) {
}

auto RecordingBackend::SetRenderer(std::unique_ptr<OverlayRenderer>) -> void {
}

auto RecordingBackend::SetInteraction(std::unique_ptr<InteractionHandler>) -> void {
}

auto RecordingBackend::Init() -> Result<> {
    return {};
}

auto RecordingBackend::Pause() -> Result<> {
    return {};
}

auto RecordingBackend::Resume() -> Result<> {
    return {};
}

auto RecordingBackend::ShouldRender() -> Result<RenderReadiness> {
    return RenderReadiness::Unable;
}

auto RecordingBackend::Render(const std::shared_ptr<Image>&, CommandBatch&, float) -> Result<bool> {
    return false;
}

auto RecordingBackend::Meta() -> std::optional<FrameMeta> {
    return FrameMeta{.extent = m_extent};
}

auto RecordingBackend::OnHover(const PointerHit& hit) -> std::optional<Haptics> {
    m_events->push_back(
        {.kind = RecordedKind::Hover, .pointer = hit.pointer, .uv = hit.uv, .mode = hit.mode, .primary = hit.primary}
    );
    return std::nullopt;
}

auto RecordingBackend::OnLeft(size_t pointer) -> void {
    m_events->push_back({.kind = RecordedKind::Left, .pointer = pointer});
}

auto RecordingBackend::OnPointer(const PointerHit& hit, bool pressed) -> void {
    m_events->push_back(
        {.kind = RecordedKind::Pointer,
         .pointer = hit.pointer,
         .uv = hit.uv,
         .pressed = pressed,
         .mode = hit.mode,
         .primary = hit.primary}
    );
}

auto RecordingBackend::OnScroll(const PointerHit& hit, float delta_y, float delta_x) -> void {
    m_events->push_back({.kind = RecordedKind::Scroll, .pointer = hit.pointer, .delta_y = delta_y, .delta_x = delta_x});
}
