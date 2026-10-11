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

#include "Hid.hpp"

auto NullHidProvider::MouseMove(const Eigen::Vector2f&) -> void {
}

auto NullHidProvider::SendButton(uint16_t, bool) -> void {
}

auto NullHidProvider::Wheel(int32_t, int32_t) -> void {
}

auto NullHidProvider::SetModifiers(uint8_t) -> void {
}

auto NullHidProvider::SendKey(VirtualKey, bool) -> void {
}

auto NullHidProvider::SetDesktopExtent(const Eigen::Vector2f&) -> void {
}

auto NullHidProvider::SetDesktopOrigin(const Eigen::Vector2f&) -> void {
}

auto NullHidProvider::Commit() -> void {
}
