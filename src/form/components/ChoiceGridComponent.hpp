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

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "FormComponent.hpp"

using ChoiceSignal = std::function<void(size_t)>;

class ChoiceGridComponent final : public FormComponent
{
  public:
    static constexpr float kGap = 6.0f;
    static constexpr int kFontSize = 14;

    ChoiceGridComponent(
        std::string name,
        const std::vector<std::string>& choices,
        std::optional<size_t> selected,
        size_t columns,
        float row_height
    );

    auto SetOnChoose(ChoiceSignal on_choose) -> void { m_on_choose = std::move(on_choose); }

  protected:
    auto Layout() -> void override;

  private:
    size_t m_columns;
    float m_row_height;
    ChoiceSignal m_on_choose;
};
