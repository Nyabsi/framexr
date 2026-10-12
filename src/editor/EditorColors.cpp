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

#include "EditorColors.hpp"

#include "Canvas.hpp"

auto EditorColors::Hex(std::string_view hex) -> Eigen::Vector4f {
    return Canvas::ParseColor(hex).value_or(Canvas::FallbackColor());
}

auto EditorColors::Readable(const Eigen::Vector4f& background) -> Eigen::Vector4f {
    const float luminance = 0.2126f * background.x() + 0.7152f * background.y() + 0.0722f * background.z();
    return luminance > 0.5f ? Hex("#181926") : Hex("#eff1f5");
}

auto EditorColors::Background() -> Eigen::Vector4f {
    return Hex("@panel");
}

auto EditorColors::Surface() -> Eigen::Vector4f {
    return Hex("@surface");
}

auto EditorColors::Accent() -> Eigen::Vector4f {
    return Hex("@accent");
}

auto EditorColors::Text() -> Eigen::Vector4f {
    return Hex("@text");
}

auto EditorColors::Dim() -> Eigen::Vector4f {
    return Hex("@subtle");
}

auto EditorColors::Dark() -> Eigen::Vector4f {
    return Hex("@shadow");
}

auto EditorColors::OnAccent() -> Eigen::Vector4f {
    return Hex("@on-accent");
}

auto EditorColors::Danger() -> Eigen::Vector4f {
    return Hex("@red");
}
