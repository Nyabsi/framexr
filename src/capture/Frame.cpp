// FrameXR
//
// This is a port of wlx-capture from Rust to C++.
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
// Copyright (C) 2023-2025 galister and wlx-capture contributors
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

#include "Frame.hpp"

#include <algorithm>

auto DmabufFrame::IsValid() const -> bool {
    return std::ranges::all_of(planes, [](const FramePlane& plane) -> bool { return plane.fd >= 0; });
}

auto FourccToString(uint32_t fourcc) -> std::string {
    std::string text;
    for (int i = 0; i < 4; ++i) {
        text.push_back(static_cast<char>((fourcc >> (i * 8)) & 0xFF));
    }
    return text;
}
