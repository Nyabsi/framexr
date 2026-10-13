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

#include "Form.hpp"

#include <utility>

Form::Form(std::string name) : FormComponent(std::move(name)) {
}

auto Form::Theme() const -> const ThemeDefinition& {
    return m_theme.empty() ? g_themes->Active() : g_themes->Find(m_theme);
}

auto Form::Build(float scale) -> FormState {
    FormState state;
    FormComponent::Build(state, Eigen::Vector2f::Zero(), scale, Theme().rounding);
    return state;
}

auto Form::FromDocument(const FormDocument& document, std::string name, ButtonBinder bind) -> std::unique_ptr<Form> {
    auto form = std::make_unique<Form>(std::move(name));
    if (document.Theme()) {
        form->SetTheme(*document.Theme());
    }
    const auto size = document.Size();
    form->SetSize({static_cast<float>(size[0]), static_cast<float>(size[1])});
    const FormContext context{.theme = form->Theme(), .bind = std::move(bind)};
    Instantiate(*form, document.Backgrounds(document.Origin()), context);
    Instantiate(*form, document.Arrange(), context);
    return form;
}

auto Form::Instantiate(FormComponent& parent, std::span<const FormElement> arranged, const FormContext& context) -> void {
    for (const auto& element : arranged) {
        FormElements::Create(parent, element, context);
    }
}
