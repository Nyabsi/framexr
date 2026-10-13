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

#include <string>
#include <string_view>

#include <Eigen/Core>

#include "FormComponent.hpp"

class OutlineComponent;

class SwatchComponent final : public FormComponent {
  public:
    static constexpr int kFontSize = 14;

    SwatchComponent(std::string name, std::string_view text, const Eigen::Vector4f& color, bool interactive = true);

  protected:
    auto Layout() -> void override;
    auto Emit(FormState& state, const FormFrame& frame) -> void override;

  private:
    std::string m_text;
    Eigen::Vector4f m_color;
    bool m_interactive;
    OutlineComponent& m_rim;
};
