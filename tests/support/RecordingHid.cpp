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

#include "RecordingHid.hpp"

auto RecordingHid::MouseMove(const Eigen::Vector2f&) -> void {
}

auto RecordingHid::SendButton(uint16_t button, bool down) -> void {
    m_buttons.push_back({.button = button, .down = down});
}

auto RecordingHid::Wheel(int32_t, int32_t) -> void {
}

auto RecordingHid::SetModifiers(uint8_t value) -> void {
    m_modifiers = value;
}

auto RecordingHid::SendKey(VirtualKey key, bool down) -> void {
    m_events.push_back({.key = key, .down = down, .modifiers = m_modifiers});
}

auto RecordingHid::SetDesktopExtent(const Eigen::Vector2f&) -> void {
}

auto RecordingHid::SetDesktopOrigin(const Eigen::Vector2f&) -> void {
}

auto RecordingHid::Commit() -> void {
}

auto RecordingHid::Clear() -> void {
    m_events.clear();
}
