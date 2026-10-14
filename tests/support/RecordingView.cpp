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

#include "RecordingView.hpp"

#include <utility>

#include "Compose.hpp"

RecordingView::RecordingView(VkExtent2D extent, std::shared_ptr<std::vector<RecordedEvent>> events)
    : View(std::make_unique<Compose>(extent)),
      m_events(std::move(events))
{
}

auto RecordingView::OnHover(const PointerHit& hit) -> std::optional<Haptics>
{
    m_events->push_back(
        {.kind = RecordedKind::Hover, .pointer = hit.pointer, .uv = hit.uv, .mode = hit.mode, .primary = hit.primary}
    );
    return std::nullopt;
}

auto RecordingView::OnLeft(size_t pointer) -> void
{
    m_events->push_back({.kind = RecordedKind::Left, .pointer = pointer});
}

auto RecordingView::OnPointer(const PointerHit& hit, bool pressed) -> void
{
    m_events->push_back(
        {.kind = RecordedKind::Press,
         .pointer = hit.pointer,
         .uv = hit.uv,
         .pressed = pressed,
         .mode = hit.mode,
         .primary = hit.primary}
    );
}

auto RecordingView::OnScroll(const PointerHit& hit, float delta_y, float delta_x) -> void
{
    m_events->push_back({.kind = RecordedKind::Scroll, .pointer = hit.pointer, .delta_y = delta_y, .delta_x = delta_x});
}
