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

RecordingBackend::RecordingBackend(VkExtent2D extent, std::shared_ptr<std::vector<RecordedEvent>> events)
    : extent_(extent),
      events_(std::move(events)) {
}

auto RecordingBackend::SetRenderer(std::unique_ptr<OverlayRenderer>) -> void {
}

auto RecordingBackend::SetInteraction(std::unique_ptr<InteractionHandler>) -> void {
}

auto RecordingBackend::Init(AppState&) -> Result<> {
    return {};
}

auto RecordingBackend::Pause(AppState&) -> Result<> {
    return {};
}

auto RecordingBackend::Resume(AppState&) -> Result<> {
    return {};
}

auto RecordingBackend::ShouldRender(AppState&) -> Result<RenderReadiness> {
    return RenderReadiness::Unable;
}

auto RecordingBackend::Render(AppState&, const std::shared_ptr<Image>&, CommandBatch&, float) -> Result<bool> {
    return false;
}

auto RecordingBackend::Meta() -> std::optional<FrameMeta> {
    return FrameMeta{.extent = extent_};
}

auto RecordingBackend::OnHover(AppState&, const PointerHit& hit) -> std::optional<Haptics> {
    events_->push_back(
        {.kind = RecordedKind::Hover, .pointer = hit.pointer, .uv = hit.uv, .mode = hit.mode, .primary = hit.primary}
    );
    return std::nullopt;
}

auto RecordingBackend::OnLeft(AppState&, size_t pointer) -> void {
    events_->push_back({.kind = RecordedKind::Left, .pointer = pointer});
}

auto RecordingBackend::OnPointer(AppState&, const PointerHit& hit, bool pressed) -> void {
    events_->push_back(
        {.kind = RecordedKind::Pointer,
         .pointer = hit.pointer,
         .uv = hit.uv,
         .pressed = pressed,
         .mode = hit.mode,
         .primary = hit.primary}
    );
}

auto RecordingBackend::OnScroll(AppState&, const PointerHit& hit, float delta_y, float delta_x) -> void {
    events_->push_back({.kind = RecordedKind::Scroll, .pointer = hit.pointer, .delta_y = delta_y, .delta_x = delta_x});
}
