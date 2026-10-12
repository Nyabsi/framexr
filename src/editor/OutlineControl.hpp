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

#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Core>

#include "Control.hpp"

class OutlineControl final : public Control {
  public:
    OutlineControl(std::optional<Eigen::Vector4f> fill, std::optional<HighlightOutline> outline);

    auto HighlightColor() -> std::optional<Eigen::Vector4f> override;
    auto Outline() -> std::optional<HighlightOutline> override;

  private:
    std::optional<Eigen::Vector4f> m_fill;
    std::optional<HighlightOutline> m_outline;
};
