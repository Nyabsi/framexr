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

#include <optional>
#include <string>

#include <Eigen/Core>

#include "FormComponent.hpp"

class OutlineComponent final : public FormComponent {
  public:
    explicit OutlineComponent(
        std::string name = {},
        const std::optional<Eigen::Vector4f>& fill = std::nullopt,
        const std::optional<HighlightOutline>& outline = std::nullopt
    );

    auto SetCornerRadius(std::optional<float> corner_radius) -> void;

  protected:
    auto Emit(FormState& state, const FormFrame& frame) -> void override;

  private:
    std::optional<float> m_corner_radius;
};
