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

#include "panels/PalettePanel.hpp"

#include "ButtonComponent.hpp"
#include "Editor.hpp"
#include "StackComponent.hpp"
#include "TextLineComponent.hpp"
#include "panels/PanelStyle.hpp"

PalettePanel::PalettePanel(Editor& editor, const Eigen::Vector2f& size)
    : FormComponent("palette"),
      m_stack(AddComponent<StackComponent>("stack", PanelStyle::kPadding, PanelStyle::kGap)) {
    SetSize(size);
    m_stack.SetSize(size);
    m_stack.SetBgColor(FormTheme::Rgba("@panel"));
    const auto text = FormTheme::Rgba("@text");
    const auto subtle = FormTheme::Rgba("@subtle");
    const auto surface = FormTheme::Rgba("@surface");

    if (editor.Target() == EditTarget::Theme) {
        m_stack.AddComponent<TextLineComponent>("title", "Theme", PanelStyle::kTitleFont, text, PanelStyle::kTitleHeight);
        m_stack.AddComponent<TextLineComponent>(
            "hint",
            "Pick a colour on the right",
            PanelStyle::kCaptionFont,
            subtle,
            PanelStyle::kCaptionHeight
        );
        m_stack.AddComponent<TextLineComponent>(
            "hint-more",
            "to change it everywhere",
            PanelStyle::kCaptionFont,
            subtle,
            PanelStyle::kCaptionHeight
        );
        return;
    }

    m_stack.AddComponent<TextLineComponent>("title", "Add", PanelStyle::kTitleFont, text, PanelStyle::kTitleHeight);
    m_stack.AddComponent<TextLineComponent>(
        "hint",
        "Drag onto the preview",
        PanelStyle::kCaptionFont,
        subtle,
        PanelStyle::kCaptionHeight
    );

    if (editor.Target() == EditTarget::Watch) {
        for (const auto part : FormDesigner::kParts) {
            auto& item = m_stack.AddComponent<ButtonComponent>("part", FormDesigner::PartName(part));
            item.SetSize({0.0f, PanelStyle::kRowHeight});
            item.SetFontSize(PanelStyle::kTextFont);
            item.SetFgColor(text);
            item.SetBgColor(surface);
            item.SetEllipsize(true);
            item.SetOnPress([&editor, part](PointerMode) -> void { editor.StartCarry(part); });
        }
        return;
    }

    for (const auto key_template : KeyboardDesigner::kTemplates) {
        auto& item = m_stack.AddComponent<ButtonComponent>("template", KeyboardDesigner::TemplateName(key_template));
        item.SetSize({0.0f, PanelStyle::kRowHeight});
        item.SetFontSize(PanelStyle::kTextFont);
        item.SetFgColor(text);
        item.SetBgColor(surface);
        item.SetEllipsize(true);
        item.SetOnPress([&editor, key_template](PointerMode) -> void { editor.StartCarry(key_template); });
    }
}
