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

#include "panels/ThemeProperties.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <string>
#include <vector>

#include "ButtonComponent.hpp"
#include "ChoiceGridComponent.hpp"
#include "Editor.hpp"
#include "LaserInputComponent.hpp"
#include "RowComponent.hpp"
#include "SectionComponent.hpp"
#include "StackComponent.hpp"
#include "SwatchComponent.hpp"
#include "TextLineComponent.hpp"
#include "panels/PanelStyle.hpp"

namespace
{

constexpr auto kRoundings = std::to_array<Rounding>({Rounding::Square, Rounding::Subtle, Rounding::Round});

}

ThemeProperties::ThemeProperties(Editor& editor, const Eigen::Vector2f& size)
    : FormComponent("theme"),
      m_stack(AddComponent<StackComponent>("stack", PanelStyle::kPadding, PanelStyle::kGap))
{
    SetSize(size);
    m_stack.SetSize(size);
    m_stack.SetBgColor(FormTheme::Rgba("@panel"));
    m_stack.AddComponent<TextLineComponent>(
        "title",
        "Theme",
        PanelStyle::kTitleFont,
        FormTheme::Rgba("@text"),
        PanelStyle::kTitleHeight
    );
    AddThemes(editor);
    AddCorners(editor);
    AddFont(editor);
    AddColours(editor);
}

auto ThemeProperties::AddThemes(Editor& editor) -> void
{
    const auto& current = editor.ThemeHistory().Current();
    const auto& themes = g_themes->All();

    m_stack.AddComponent<SectionComponent>("themes", "Themes");

    std::vector<std::string> names;
    std::optional<size_t> selected;
    for (const auto& theme : themes)
    {
        if (theme.name == current.name)
        {
            selected = names.size();
        }
        names.push_back(theme.name);
    }
    auto& list = m_stack.AddComponent<ChoiceGridComponent>("list", names, selected, 2, PanelStyle::kListRow);
    list.SetOnChoose([&editor, names](size_t index) -> void { editor.SelectTheme(names.at(index)); });

    auto& actions = m_stack.AddComponent<RowComponent>("actions", PanelStyle::kRowHeight, PanelStyle::kGap);

    auto& create = actions.AddComponent<ButtonComponent>("new", "New theme");
    create.SetFontSize(PanelStyle::kTextFont);
    create.SetFgColor(FormTheme::Rgba("@text"));
    create.SetBgColor(FormTheme::Rgba("@surface"));
    create.SetEllipsize(true);
    create.SetOnRelease([&editor]() -> void { editor.NewTheme(); });

    if (!FormTheme::IsBuiltIn(current.name) && !current.source.empty())
    {
        auto& remove = actions.AddComponent<ButtonComponent>("delete", editor.DeleteArmed() ? "Sure?" : "Delete");
        remove.SetFontSize(PanelStyle::kTextFont);
        remove.SetFgColor(FormTheme::Rgba("@on-accent"));
        remove.SetBgColor(FormTheme::Rgba("@red"));
        remove.SetEllipsize(true);
        remove.SetOnRelease([&editor]() -> void { editor.DeleteTheme(); });
    }

    if (!FormTheme::IsBuiltIn(current.name))
    {
        m_stack.AddComponent<LaserInputComponent>(
            "theme.name",
            "Name",
            editor.Text(),
            current.name,
            [&editor](const std::string& text) -> void { editor.RenameTheme(text); }
        );
    }
}

auto ThemeProperties::AddCorners(Editor& editor) -> void
{
    const auto& current = editor.ThemeHistory().Current();

    m_stack.AddComponent<SectionComponent>("corners", "Corners");

    std::vector<std::string> names;
    std::optional<size_t> selected;
    for (const auto rounding : kRoundings)
    {
        if (current.rounding == rounding)
        {
            selected = names.size();
        }
        names.emplace_back(FormTheme::Name(rounding));
    }
    auto& roundings = m_stack.AddComponent<ChoiceGridComponent>(
        "roundings",
        names,
        selected,
        kRoundings.size(),
        PanelStyle::kRowHeight
    );
    roundings.SetOnChoose(
        [&editor](size_t index) -> void
        { editor.EditTheme([index](ThemeDefinition& edited) -> void { edited.rounding = kRoundings.at(index); }); }
    );
}

auto ThemeProperties::AddFont(Editor& editor) -> void
{
    const auto& font = editor.ThemeHistory().Current().font;
    const bool picking = editor.Dialog() == DialogKind::Font;

    m_stack.AddComponent<SectionComponent>("font", "Font");

    auto& row = m_stack.AddComponent<RowComponent>("font-row", PanelStyle::kRowHeight, PanelStyle::kGap);
    row.SetWeights({1.0f, 2.0f});
    row.AddComponent<TextLineComponent>("caption", "Font", PanelStyle::kTextFont, FormTheme::Rgba("@subtle"));
    auto& pick = row.AddComponent<ButtonComponent>("pick", font.value_or("Default"));
    pick.SetFontSize(PanelStyle::kTextFont);
    pick.SetFgColor(FormTheme::Rgba(picking ? "@on-accent" : "@text"));
    pick.SetBgColor(FormTheme::Rgba(picking ? "@accent" : "@surface"));
    pick.SetEllipsize(true);
    pick.SetOnRelease([&editor]() -> void { editor.OpenFonts(); });
}

auto ThemeProperties::AddColours(Editor& editor) -> void
{
    const auto& current = editor.ThemeHistory().Current();

    m_stack.AddComponent<SectionComponent>("colours", "Colours");
    m_stack.AddComponent<TextLineComponent>(
        "hint",
        "Elements using @name follow these",
        PanelStyle::kCaptionFont,
        FormTheme::Rgba("@subtle"),
        PanelStyle::kCaptionHeight
    );

    for (size_t start = 0; start < current.colors.size(); start += 2)
    {
        auto& row = m_stack.AddComponent<RowComponent>("colour-row", PanelStyle::kListRow, PanelStyle::kGap);
        for (size_t i = start; i < start + 2; ++i)
        {
            if (i >= current.colors.size())
            {
                row.AddComponent<FormComponent>("empty");
                continue;
            }
            const auto name = current.colors[i].name;
            auto& swatch = row.AddComponent<SwatchComponent>(name, name, FormTheme::Rgba(current.colors[i].hex));
            swatch.SetOnRelease(
                [&editor, name]() -> void
                {
                    editor.OpenColor({
                        .title = FormTheme::Token(name),
                        .get = [&editor, name]() -> std::string
                        { return FormTheme::Lookup(editor.ThemeHistory().Current(), name).value_or("#ffffff"); },
                        .set = [&editor, name](const std::string& picked) -> void
                        {
                            editor.EditTheme(
                                [&name, &picked](ThemeDefinition& edited) -> void
                                {
                                    for (auto& entry : edited.colors)
                                    {
                                        if (entry.name == name)
                                        {
                                            entry.hex = picked;
                                        }
                                    }
                                }
                            );
                        },
                        .reset = nullptr,
                        .tokens = false,
                    });
                }
            );
        }
    }
}
