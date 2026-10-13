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

#include "ColorFieldComponent.hpp"

#include <utility>

#include "FormTheme.hpp"
#include "SwatchComponent.hpp"
#include "TextLineComponent.hpp"

ColorFieldComponent::ColorFieldComponent(std::string name, std::string_view caption, const std::string& color)
    : FormComponent(std::move(name)),
      m_caption(AddComponent<TextLineComponent>("caption", caption, kFontSize, FormTheme::Rgba("@subtle"))),
      m_swatch(AddComponent<SwatchComponent>("swatch", color, FormTheme::Rgba(color))) {
    SetSize({0.0f, kHeight});
}

auto ColorFieldComponent::Layout() -> void {
    const float available = Size().x() - kGap;
    const float caption = available * 2.2f / 5.0f;
    m_caption.SetRect({0.0f, 0.0f, caption, Size().y()});
    m_swatch.SetRect({caption + kGap, 0.0f, available - caption, Size().y()});
}
