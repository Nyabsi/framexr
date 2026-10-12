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

#include "OutlineControl.hpp"

#include <utility>

#include "Control.hpp"

OutlineControl::OutlineControl(std::optional<Eigen::Vector4f> fill, std::optional<HighlightOutline> outline)
    : m_fill(std::move(fill)),
      m_outline(std::move(outline)) {
}

auto OutlineControl::HighlightColor() -> std::optional<Eigen::Vector4f> {
    return m_fill;
}

auto OutlineControl::Outline() -> std::optional<HighlightOutline> {
    return m_outline;
}
