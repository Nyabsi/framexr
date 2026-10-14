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

#include "LaserInputComponent.hpp"

#include <format>
#include <utility>

#include "ButtonComponent.hpp"
#include "FormTheme.hpp"
#include "TextLineComponent.hpp"

LaserInputComponent::LaserInputComponent(
    std::string name,
    std::string_view caption,
    TextEntry& entry,
    std::string value,
    TextCommit commit
)
    : FormComponent(name),
      m_caption(AddComponent<TextLineComponent>("caption", caption, kCaptionFont, FormTheme::Rgba("@subtle"))),
      m_field(AddComponent<ButtonComponent>("field"))
{
    const bool focused = entry.Editing(name);
    if (focused)
    {
        m_field.SetText(std::format("{}_", entry.Buffer()));
    }
    else
    {
        m_field.SetText(value.empty() ? std::string("(empty)") : value);
    }
    m_field.SetFontSize(kFieldFont);
    m_field.SetBgColor(FormTheme::Rgba(focused ? "@surface" : "@shadow"));
    m_field.SetFgColor(FormTheme::Rgba(focused ? "@accent" : "@text"));
    m_field.SetEllipsize(true);
    m_field.SetOnRelease(
        [&entry, name = std::move(name), value = std::move(value), commit = std::move(commit)]() -> void
        { entry.Begin(name, value, commit); }
    );
    SetSize({0.0f, kCaptionHeight + kGap + kFieldHeight});
}

auto LaserInputComponent::Layout() -> void
{
    m_caption.SetRect({0.0f, 0.0f, Size().x(), kCaptionHeight});
    m_field.SetRect({0.0f, kCaptionHeight + kGap, Size().x(), kFieldHeight});
}
