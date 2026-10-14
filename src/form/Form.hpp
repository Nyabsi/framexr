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

#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "FormComponent.hpp"
#include "FormTheme.hpp"
#include "document/Common.hpp"
#include "document/FormElements.hpp"
#include "document/FormDocument.hpp"

class Form final : public FormComponent
{
  public:
    explicit Form(std::string name = {});

    static auto FromDocument(const FormDocument& document, std::string name = {}, ButtonBinder bind = {}) -> std::unique_ptr<Form>;
    static auto Instantiate(FormComponent& parent, std::span<const FormElement> arranged, const FormContext& context) -> void;

    [[nodiscard]] auto ThemeName() const -> const std::string& { return m_theme; }
    [[nodiscard]] auto Theme() const -> const ThemeDefinition&;

    auto SetTheme(std::string_view name) -> void { m_theme = name; }

    [[nodiscard]] auto Build(float scale = 1.0f) -> FormState;

  private:
    std::string m_theme;
};
