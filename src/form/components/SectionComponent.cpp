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

#include "SectionComponent.hpp"

#include <utility>

#include "FormTheme.hpp"
#include "PanelComponent.hpp"
#include "TextLineComponent.hpp"

SectionComponent::SectionComponent(std::string name, std::string_view title)
    : FormComponent(std::move(name)),
      m_divider(AddComponent<PanelComponent>("divider")),
      m_title(AddComponent<TextLineComponent>("title", title, kFontSize, FormTheme::Rgba("@accent"))) {
    auto divider = FormTheme::Rgba("@subtle");
    divider.w() = 0.25f;
    m_divider.SetBgColor(divider);
    m_divider.SetThemeShape(Shape::Control);
    m_divider.SetCornerRadius(0.0f);
    SetSize({0.0f, 2.0f * kSpacing + kTitleHeight});
}

auto SectionComponent::Layout() -> void {
    m_divider.SetRect({0.0f, kSpacing, Size().x(), 1.0f});
    m_title.SetRect({0.0f, 2.0f * kSpacing, Size().x(), kTitleHeight});
}
