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

#include "panels/FontPanel.hpp"

#include <format>
#include <optional>
#include <string>

#include "ButtonComponent.hpp"
#include "Editor.hpp"
#include "FontCache.hpp"
#include "LaserInputComponent.hpp"
#include "StackComponent.hpp"
#include "TextLineComponent.hpp"
#include "TextSearch.hpp"
#include "panels/PanelStyle.hpp"

namespace {

constexpr size_t kResults = 16;

}

FontPanel::FontPanel(Editor& editor, const Eigen::Vector2f& size)
    : FormComponent("fonts"),
      m_stack(AddComponent<StackComponent>("stack", PanelStyle::kPadding, PanelStyle::kGap)) {
    SetSize(size);
    m_stack.SetSize(size);
    m_stack.SetBgColor(FormTheme::Rgba("@panel"));
    if (editor.Target() != EditTarget::Theme || g_fonts == nullptr) {
        return;
    }
    const auto text = FormTheme::Rgba("@text");
    const auto subtle = FormTheme::Rgba("@subtle");
    const auto surface = FormTheme::Rgba("@surface");
    const auto accent = FormTheme::Rgba("@accent");
    const auto on_accent = FormTheme::Rgba("@on-accent");
    const auto& current = editor.ThemeHistory().Current().font;
    const auto choose = [&editor](std::optional<std::string> family) -> std::function<void()> {
        return [&editor, family = std::move(family)]() -> void {
            editor.EditTheme([&family](ThemeDefinition& edited) -> void { edited.font = family; });
            editor.Text().End();
            editor.CloseDialog();
        };
    };

    m_stack.AddComponent<TextLineComponent>("title", "Font", PanelStyle::kTitleFont, text, PanelStyle::kTitleHeight);

    m_stack.AddComponent<LaserInputComponent>(
        "font.search",
        "Search, like noto or inter",
        editor.Text(),
        editor.FontQuery(),
        [&editor](const std::string& query) -> void { editor.SetFontQuery(query); }
    );

    const bool is_default = !current.has_value();
    auto& fallback = m_stack.AddComponent<ButtonComponent>("default", "Default");
    fallback.SetSize({0.0f, PanelStyle::kRowHeight});
    fallback.SetFontSize(PanelStyle::kTextFont);
    fallback.SetFgColor(is_default ? on_accent : text);
    fallback.SetBgColor(is_default ? accent : surface);
    fallback.SetEllipsize(true);
    fallback.SetOnRelease(choose(std::nullopt));

    const bool searching = editor.Text().Editing("font.search");
    const auto& query = searching ? editor.Text().Buffer() : editor.FontQuery();
    size_t shown = 0;
    size_t matches = 0;
    for (const auto& family : g_fonts->Families()) {
        if (!TextSearch::Matches(family, query)) {
            continue;
        }
        ++matches;
        if (shown >= kResults) {
            continue;
        }
        const bool chosen = current == family;
        auto& row = m_stack.AddComponent<ButtonComponent>(family, family);
        row.SetSize({0.0f, PanelStyle::kCompactRow});
        row.SetFontSize(PanelStyle::kTextFont);
        row.SetFgColor(chosen ? on_accent : text);
        row.SetBgColor(chosen ? accent : surface);
        row.SetEllipsize(true);
        row.SetOnRelease(choose(family));
        ++shown;
    }
    if (matches > shown) {
        const auto more = std::format("{} more, keep typing to narrow it down", matches - shown);
        m_stack.AddComponent<TextLineComponent>("more", more, PanelStyle::kCaptionFont, subtle, PanelStyle::kCaptionHeight);
    } else if (matches == 0) {
        m_stack.AddComponent<TextLineComponent>(
            "none",
            "No font matches that",
            PanelStyle::kCaptionFont,
            subtle,
            PanelStyle::kCaptionHeight
        );
    }

    auto& done = m_stack.AddComponent<ButtonComponent>("done", "Done");
    done.SetSize({0.0f, PanelStyle::kRowHeight});
    done.SetFontSize(PanelStyle::kTextFont);
    done.SetFgColor(text);
    done.SetBgColor(surface);
    done.SetEllipsize(true);
    done.SetOnRelease([&editor]() -> void {
        editor.Text().End();
        editor.CloseDialog();
    });
}
