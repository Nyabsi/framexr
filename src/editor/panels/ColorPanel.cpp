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

#include "panels/ColorPanel.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

#include "ButtonComponent.hpp"
#include "ColorPickerComponent.hpp"
#include "Editor.hpp"
#include "LaserInputComponent.hpp"
#include "RowComponent.hpp"
#include "StackComponent.hpp"
#include "SwatchComponent.hpp"
#include "TextLineComponent.hpp"
#include "panels/PanelStyle.hpp"

namespace {

constexpr int kHues = 12;

}

ColorPanel::ColorPanel(Editor& editor, const Eigen::Vector2f& size)
    : FormComponent("colour"),
      m_stack(AddComponent<StackComponent>("stack", PanelStyle::kPadding, PanelStyle::kGap)) {
    SetSize(size);
    m_stack.SetSize(size);
    m_stack.SetBgColor(FormTheme::Rgba("@panel"));
    const auto& target = editor.Colors().Target();
    if (!target) {
        return;
    }
    const auto subtle = FormTheme::Rgba("@subtle");
    const auto current = target->get();

    m_stack.AddComponent<TextLineComponent>(
        "title",
        target->title,
        PanelStyle::kTitleFont,
        FormTheme::Rgba("@text"),
        PanelStyle::kTitleHeight
    );
    auto& shown = m_stack.AddComponent<SwatchComponent>("current", current, FormTheme::Rgba(current), false);
    shown.SetSize({0.0f, 36.0f});

    m_stack.AddComponent<LaserInputComponent>(
        "color.hex",
        "Hex, like #8aadf4 or @accent",
        editor.Text(),
        current,
        [&editor](const std::string& typed) -> void {
            const auto color = typed.starts_with('#') || typed.starts_with('@') ? typed : std::format("#{}", typed);
            if (!FormTheme::ParseColor(color)) {
                Editor::Notify(std::format("{} is not a colour", typed));
                return;
            }
            editor.Colors().Pick(color);
            editor.Touch();
        }
    );

    AddPicker(editor);

    m_stack.AddComponent<TextLineComponent>(
        "theme",
        target->tokens ? "Theme, follows theme changes" : "Theme",
        PanelStyle::kCaptionFont,
        subtle,
        PanelStyle::kCaptionHeight
    );
    std::vector<std::string> palette;
    for (const auto& color : g_themes->Active().colors) {
        palette.push_back(target->tokens ? FormTheme::Token(color.name) : color.hex);
    }
    AddSwatches(editor, palette, 8);

    if (const auto& recent = editor.Colors().Recent(); !recent.empty()) {
        m_stack.AddComponent<TextLineComponent>("recent", "Recent", PanelStyle::kCaptionFont, subtle, PanelStyle::kCaptionHeight);
        const std::vector<std::string> colors(recent.begin(), recent.end());
        AddSwatches(editor, colors, 8);
    }

    AddHues(editor);

    m_stack.AddComponent<TextLineComponent>("shade", "Shade", PanelStyle::kCaptionFont, subtle, PanelStyle::kCaptionHeight);
    std::vector<std::string> shades;
    for (const float value : {1.0f, 0.8f, 0.6f, 0.4f}) {
        for (const float saturation : {0.15f, 0.35f, 0.55f, 0.75f, 0.95f}) {
            shades.push_back(ColorPickerComponent::Hex(editor.Colors().Hsv()[0], saturation, value));
        }
    }
    AddSwatches(editor, shades, 5);

    AddFooter(editor);
}

auto ColorPanel::AddPicker(Editor& editor) -> void {
    auto& colors = editor.Colors();
    const float width = Size().x() - 2.0f * PanelStyle::kPadding;
    const float top = m_stack.Bottom();
    auto wheel = ColorPickerComponent::WheelRect(width);
    wheel.x += PanelStyle::kPadding;
    wheel.y += top;
    auto bar = ColorPickerComponent::BarRect(width);
    bar.x += PanelStyle::kPadding;
    bar.y += top;
    colors.SetRects(wheel, bar);
    m_stack.AddComponent<ColorPickerComponent>("picker", colors.Hsv(), colors.WheelImage(), colors.BarImage());
}

auto ColorPanel::AddSwatches(Editor& editor, std::span<const std::string> colors, size_t columns) -> void {
    const auto current = editor.Colors().Target()->get();
    for (size_t start = 0; start < colors.size(); start += columns) {
        auto& row = m_stack.AddComponent<RowComponent>("swatches", PanelStyle::kCompactRow, PanelStyle::kGap);
        for (size_t i = start; i < start + columns; ++i) {
            if (i >= colors.size()) {
                row.AddComponent<FormComponent>("empty");
                continue;
            }
            const auto& color = colors[i];
            auto& swatch = row.AddComponent<SwatchComponent>(color, color == current ? "*" : "", FormTheme::Rgba(color));
            swatch.SetOnRelease([&editor, color]() -> void {
                editor.Colors().Pick(color);
                editor.Touch();
            });
        }
    }
}

auto ColorPanel::AddHues(Editor& editor) -> void {
    const float hue = editor.Colors().Hsv()[0];
    m_stack.AddComponent<TextLineComponent>(
        "hue",
        "Hue",
        PanelStyle::kCaptionFont,
        FormTheme::Rgba("@subtle"),
        PanelStyle::kCaptionHeight
    );
    auto& row = m_stack.AddComponent<RowComponent>("hues", 26.0f, PanelStyle::kGap);
    for (int i = 0; i < kHues; ++i) {
        const float shade = static_cast<float>(i) / kHues;
        auto& button = row.AddComponent<ButtonComponent>("hue", std::abs(shade - hue) < 0.5f / kHues ? "*" : "");
        button.SetFontSize(PanelStyle::kTextFont);
        button.SetFgColor(FormTheme::Rgba("@on-accent"));
        button.SetBgColor(FormTheme::Rgba(ColorPickerComponent::Hex(shade, 0.7f, 0.95f)));
        button.SetEllipsize(true);
        button.SetOnRelease([&editor, shade]() -> void {
            auto& colors = editor.Colors();
            if (!colors.Target()) {
                return;
            }
            const auto& hsv = colors.Hsv();
            colors.SetHsv({shade, std::max(hsv[1], 0.5f), std::max(hsv[2], 0.5f)});
            editor.Touch();
        });
    }
}

auto ColorPanel::AddFooter(Editor& editor) -> void {
    const auto text = FormTheme::Rgba("@text");
    const auto surface = FormTheme::Rgba("@surface");
    auto& row = m_stack.AddComponent<RowComponent>("footer", PanelStyle::kRowHeight, PanelStyle::kGap);
    if (editor.Colors().Target()->reset) {
        auto& reset = row.AddComponent<ButtonComponent>("reset", "Theme default");
        reset.SetFontSize(PanelStyle::kTextFont);
        reset.SetFgColor(text);
        reset.SetBgColor(surface);
        reset.SetEllipsize(true);
        reset.SetOnRelease([&editor]() -> void {
            if (const auto& target = editor.Colors().Target()) {
                target->reset();
            }
            editor.Touch();
        });
    }
    auto& done = row.AddComponent<ButtonComponent>("done", "Done");
    done.SetFontSize(PanelStyle::kTextFont);
    done.SetFgColor(text);
    done.SetBgColor(surface);
    done.SetEllipsize(true);
    done.SetOnRelease([&editor]() -> void { editor.CloseDialog(); });
}
