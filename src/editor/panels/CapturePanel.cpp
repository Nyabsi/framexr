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

#include "panels/CapturePanel.hpp"

#include "ButtonComponent.hpp"
#include "Editor.hpp"
#include "StackComponent.hpp"
#include "TextLineComponent.hpp"
#include "panels/PanelStyle.hpp"

CapturePanel::CapturePanel(Editor& editor, const Eigen::Vector2f& size)
    : FormComponent("capture"),
      m_stack(AddComponent<StackComponent>("stack", PanelStyle::kPadding, PanelStyle::kGap)) {
    SetSize(size);
    m_stack.SetSize(size);
    m_stack.SetBgColor(FormTheme::Rgba("@panel"));
    const auto text = FormTheme::Rgba("@text");
    const auto subtle = FormTheme::Rgba("@subtle");
    const auto surface = FormTheme::Rgba("@surface");
    const bool shortcut = editor.Text().CapturingShortcut();

    m_stack.AddComponent<TextLineComponent>(
        "title",
        shortcut ? "Record shortcut" : "Press a key",
        PanelStyle::kTitleFont,
        text,
        PanelStyle::kTitleHeight
    );
    m_stack.AddComponent<TextLineComponent>(
        "hint",
        shortcut ? "Tap modifiers, then the key" : "Press it on the keyboard",
        PanelStyle::kCaptionFont,
        subtle,
        PanelStyle::kCaptionHeight
    );
    if (shortcut) {
        m_stack.AddComponent<TextLineComponent>(
            "held",
            VirtualKeys::Chord(editor.Text().Held()),
            PanelStyle::kCaptionFont,
            subtle,
            PanelStyle::kCaptionHeight
        );
    }

    auto& cancel = m_stack.AddComponent<ButtonComponent>("cancel", "Cancel");
    cancel.SetSize({0.0f, PanelStyle::kRowHeight});
    cancel.SetFontSize(PanelStyle::kTextFont);
    cancel.SetFgColor(text);
    cancel.SetBgColor(surface);
    cancel.SetEllipsize(true);
    cancel.SetOnRelease([&editor]() -> void { editor.Text().End(); });
}
