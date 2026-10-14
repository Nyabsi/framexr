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

class StackComponent final : public FormComponent
{
  public:
    StackComponent(std::string name, float padding, float gap);

    [[nodiscard]] auto Bottom() const -> float;

    auto SetBgColor(const Eigen::Vector4f& color) -> void;

  protected:
    auto Layout() -> void override;
    auto Emit(FormState& state, const FormFrame& frame) -> void override;

  private:
    float m_padding;
    float m_gap;
    std::optional<Eigen::Vector4f> m_background;
};
