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

#include "ChoiceGridComponent.hpp"

#include <utility>

#include "ButtonComponent.hpp"
#include "FormTheme.hpp"

ChoiceGridComponent::ChoiceGridComponent(
    std::string name,
    const std::vector<std::string>& choices,
    std::optional<size_t> selected,
    size_t columns,
    float row_height
)
    : FormComponent(std::move(name)),
      m_columns(columns),
      m_row_height(row_height) {
    for (size_t i = 0; i < choices.size(); ++i) {
        const bool chosen = selected == i;
        auto& choice = AddComponent<ButtonComponent>(choices[i], choices[i]);
        choice.SetFontSize(kFontSize);
        choice.SetFgColor(FormTheme::Rgba(chosen ? "@on-accent" : "@text"));
        choice.SetBgColor(FormTheme::Rgba(chosen ? "@accent" : "@surface"));
        choice.SetEllipsize(true);
        choice.SetOnRelease([this, i]() -> void {
            if (m_on_choose) {
                m_on_choose(i);
            }
        });
    }
    const size_t rows = (choices.size() + columns - 1) / columns;
    SetSize({0.0f, rows == 0 ? 0.0f : static_cast<float>(rows) * (row_height + kGap) - kGap});
}

auto ChoiceGridComponent::Layout() -> void {
    const float width = (Size().x() - kGap * static_cast<float>(m_columns - 1)) / static_cast<float>(m_columns);
    const auto& children = Children();
    for (size_t i = 0; i < children.size(); ++i) {
        const auto column = static_cast<float>(i % m_columns);
        const auto row = static_cast<float>(i / m_columns);
        children[i]->SetRect({column * (width + kGap), row * (m_row_height + kGap), width, m_row_height});
    }
}
