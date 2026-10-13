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

#include "StepperComponent.hpp"

#include <array>
#include <utility>

#include "ButtonComponent.hpp"
#include "FormTheme.hpp"
#include "PanelComponent.hpp"
#include "TextLineComponent.hpp"

StepperComponent::StepperComponent(std::string name, std::string_view label, std::string_view value)
    : FormComponent(std::move(name)),
      m_caption(AddComponent<TextLineComponent>("label", label, kFontSize, FormTheme::Rgba("@subtle"))),
      m_well(AddComponent<PanelComponent>("well")),
      m_value(m_well.AddComponent<TextLineComponent>("value", value, kFontSize, FormTheme::Rgba("@text"))),
      m_minus(&AddComponent<ButtonComponent>("minus", "-")),
      m_plus(&AddComponent<ButtonComponent>("plus", "+")) {
    m_well.SetBgColor(FormTheme::Rgba("@shadow"));
    m_well.SetThemeShape(Shape::Control);
    m_value.SetCentered(true);
    m_minus->SetFontSize(kFontSize);
    m_minus->SetFgColor(FormTheme::Rgba("@text"));
    m_minus->SetBgColor(FormTheme::Rgba("@surface"));
    m_minus->SetEllipsize(true);
    m_plus->SetFontSize(kFontSize);
    m_plus->SetFgColor(FormTheme::Rgba("@text"));
    m_plus->SetBgColor(FormTheme::Rgba("@surface"));
    m_plus->SetEllipsize(true);
    SetSize({0.0f, kHeight});
}

auto StepperComponent::Layout() -> void {
    constexpr std::array kWeights{2.2f, 1.4f, 0.7f, 0.7f};
    const std::array<FormComponent*, 4> cells{&m_caption, &m_well, m_minus, m_plus};
    const float available = Size().x() - kGap * static_cast<float>(cells.size() - 1);
    float x = 0.0f;
    for (size_t i = 0; i < cells.size(); ++i) {
        const float width = available * kWeights.at(i) / 5.0f;
        cells.at(i)->SetRect({x, 0.0f, width, Size().y()});
        x += width + kGap;
    }
    m_value.SetSize(m_well.Size());
}
