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

#include "FormComponent.hpp"

class ButtonComponent;

class PanelComponent;

class TextLineComponent;

class StepperComponent final : public FormComponent {
  public:
    static constexpr float kHeight = 34.0f;
    static constexpr float kGap = 6.0f;
    static constexpr int kFontSize = 14;

    StepperComponent(std::string name, std::string_view label, std::string_view value);

    [[nodiscard]] auto Minus() -> ButtonComponent& { return *m_minus; }
    [[nodiscard]] auto Plus() -> ButtonComponent& { return *m_plus; }

  protected:
    auto Layout() -> void override;

  private:
    TextLineComponent& m_caption;
    PanelComponent& m_well;
    TextLineComponent& m_value;
    ButtonComponent* m_minus;
    ButtonComponent* m_plus;
};
