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

#include "PanelBuilder.hpp"

#include <algorithm>
#include <format>
#include <numeric>
#include <utility>

#include "CallbackControl.hpp"
#include "Device.hpp"
#include "EditorColors.hpp"
#include "Font.hpp"
#include "OutlineControl.hpp"
#include "Theme.hpp"

namespace {

constexpr int kTitleFont = 18;

constexpr int kTextFont = 14;

constexpr int kCaptionFont = 12;

constexpr int kSectionFont = 14;

auto AppendUtf8(std::string& text, uint32_t codepoint) -> void {
    const auto byte = [&text](uint32_t value) -> void { text += static_cast<char>(static_cast<unsigned char>(value)); };
    if (codepoint < 0x80U) {
        byte(codepoint);
    } else if (codepoint < 0x800U) {
        byte(0xC0U | (codepoint >> 6U));
        byte(0x80U | (codepoint & 0x3FU));
    } else if (codepoint < 0x10000U) {
        byte(0xE0U | (codepoint >> 12U));
        byte(0x80U | ((codepoint >> 6U) & 0x3FU));
        byte(0x80U | (codepoint & 0x3FU));
    } else {
        byte(0xF0U | (codepoint >> 18U));
        byte(0x80U | ((codepoint >> 12U) & 0x3FU));
        byte(0x80U | ((codepoint >> 6U) & 0x3FU));
        byte(0x80U | (codepoint & 0x3FU));
    }
}

auto WithAlpha(Eigen::Vector4f color, float alpha) -> Eigen::Vector4f {
    color.w() = alpha;
    return color;
}

} // namespace

PanelBuilder::PanelBuilder(Canvas& canvas)
    : m_canvas(canvas),
      m_width(static_cast<float>(canvas.Width())),
      m_radius(Theme::Radius(g_themes->Active().rounding, Shape::Control)) {
    m_canvas.SetBgColor(EditorColors::Background());
    m_canvas
        .Panel(0.0f, 0.0f, m_width, static_cast<float>(canvas.Height()), Theme::Radius(g_themes->Active().rounding, Shape::Panel));
}

auto PanelBuilder::TextWidth(std::string_view text, int font_size) const -> float {
    if (g_fonts && g_device) {
        if (const auto size = g_fonts->MeasureText(text, font_size)) {
            return size->width;
        }
    }
    return 0.6f * static_cast<float>(font_size) * static_cast<float>(text.size());
}

auto PanelBuilder::Ellipsize(std::string_view text, int font_size, float width) const -> std::string {
    if (TextWidth(text, font_size) <= width) {
        return std::string(text);
    }
    auto codepoints = FontCache::DecodeUtf8(text);
    std::string shortened;
    while (!codepoints.empty()) {
        codepoints.pop_back();
        shortened.clear();
        for (const auto codepoint : codepoints) {
            AppendUtf8(shortened, codepoint);
        }
        shortened += "...";
        if (TextWidth(shortened, font_size) <= width) {
            return shortened;
        }
    }
    return "...";
}

auto PanelBuilder::Label(
    float x,
    float y,
    float w,
    float h,
    std::string_view text,
    int font_size,
    const Eigen::Vector4f& color,
    bool centered
) -> void {
    m_canvas.SetFontSize(font_size);
    m_canvas.SetFgColor(color);
    auto shown = Ellipsize(text, font_size, std::max(w - (centered ? 12.0f : 0.0f), 8.0f));
    if (centered) {
        m_canvas.LabelCentered(x, y, w, h, 0.0f, std::move(shown));
    } else {
        m_canvas.Label(x, y + static_cast<float>(font_size), w, h, 0.0f, std::move(shown));
    }
}

auto PanelBuilder::Rim(float x, float y, float w, float h) -> void {
    m_canvas.Decoration(
        x,
        y,
        w,
        h,
        m_radius,
        std::make_unique<OutlineControl>(
            std::nullopt,
            HighlightOutline{.color = WithAlpha(EditorColors::Dim(), 0.35f), .width = 1.0f}
        )
    );
}

auto PanelBuilder::Title(std::string_view text) -> void {
    Label(kPadding, m_y, m_width - 2.0f * kPadding, 24.0f, text, kTitleFont, EditorColors::Text(), false);
    m_y += 24.0f + kGap;
}

auto PanelBuilder::Section(std::string_view text) -> void {
    m_y += kGap;
    m_canvas.SetBgColor(WithAlpha(EditorColors::Dim(), 0.25f));
    m_canvas.Panel(kPadding, m_y, m_width - 2.0f * kPadding, 1.0f, 0.0f);
    m_y += kGap;
    Label(kPadding, m_y, m_width - 2.0f * kPadding, 18.0f, text, kSectionFont, EditorColors::Accent(), false);
    m_y += 18.0f + kGap;
}

auto PanelBuilder::Caption(std::string_view text) -> void {
    Label(kPadding, m_y, m_width - 2.0f * kPadding, 16.0f, text, kCaptionFont, EditorColors::Dim(), false);
    m_y += 16.0f + kGap;
}

auto PanelBuilder::Row(std::vector<Cell> cells, float height) -> void {
    for (auto& cell : cells) {
        if (cell.weight <= 0.0f) {
            cell.weight = TextWidth(cell.text, kTextFont) + 24.0f;
        }
    }
    const float total = std::accumulate(cells.begin(), cells.end(), 0.0f, [](float sum, const Cell& cell) -> float {
        return sum + cell.weight;
    });
    const float available = m_width - 2.0f * kPadding - kGap * static_cast<float>(cells.size() - 1);
    float x = kPadding;
    for (auto& cell : cells) {
        const float w = available * cell.weight / total;
        const bool interactive = cell.click || cell.press || cell.scroll;
        const auto bg = cell.selected ? EditorColors::Accent() : cell.color.value_or(EditorColors::Surface());
        const auto fg = cell.text_color.value_or(cell.selected ? EditorColors::OnAccent() : EditorColors::Text());
        m_canvas.SetFontSize(kTextFont);
        m_canvas.SetFgColor(fg);
        m_canvas.SetBgColor(bg);
        if (interactive) {
            m_canvas.Button(
                x,
                m_y,
                w,
                height,
                m_radius,
                Ellipsize(cell.text, kTextFont, std::max(w - 12.0f, 8.0f)),
                std::make_unique<CallbackControl>(std::move(cell.click), std::move(cell.press), std::move(cell.scroll))
            );
        } else {
            if (cell.color || cell.selected) {
                m_canvas.Panel(x, m_y, w, height, m_radius);
            }
            Label(x, m_y, w, height, cell.text, kTextFont, fg, cell.color.has_value() || cell.selected);
        }
        if (cell.rim) {
            Rim(x, m_y, w, height);
        }
        x += w + kGap;
    }
    m_y += height + kGap;
}

auto PanelBuilder::Button(Cell cell) -> void {
    std::vector<Cell> cells;
    cells.push_back(std::move(cell));
    Row(std::move(cells));
}

auto PanelBuilder::Stepper(std::string_view label, std::string_view value, Click minus, Click plus, ScrollCallback scroll) -> void {
    std::vector<Cell> cells;
    cells.push_back({.text = std::string(label), .weight = 2.2f, .text_color = EditorColors::Dim()});
    cells.push_back({.text = std::string(value), .weight = 1.4f, .color = EditorColors::Dark(), .scroll = std::move(scroll)});
    cells.push_back({.text = "-", .click = std::move(minus), .weight = 0.7f});
    cells.push_back({.text = "+", .click = std::move(plus), .weight = 0.7f});
    Row(std::move(cells));
}

auto PanelBuilder::Field(std::string_view label, std::string_view value, bool focused, Click focus) -> void {
    Caption(label);
    const auto shown = focused ? std::format("{}_", value) : std::string(value.empty() ? "(empty)" : value);
    std::vector<Cell> cells;
    cells.push_back({
        .text = shown,
        .click = std::move(focus),
        .color = focused ? EditorColors::Surface() : EditorColors::Dark(),
        .text_color = focused ? EditorColors::Accent() : EditorColors::Text(),
    });
    Row(std::move(cells));
}

auto PanelBuilder::Swatch(std::string_view label, std::string_view color, Click open) -> void {
    std::vector<Cell> cells;
    cells.push_back({.text = std::string(label), .weight = 2.2f, .text_color = EditorColors::Dim()});
    const auto swatch = EditorColors::Hex(color);
    cells.push_back({
        .text = std::string(color),
        .click = std::move(open),
        .weight = 2.8f,
        .color = swatch,
        .text_color = EditorColors::Readable(swatch),
        .rim = true,
    });
    Row(std::move(cells));
}

auto PanelBuilder::Swatches(
    std::span<const std::string> colors,
    std::string_view current,
    const std::function<void(const std::string&)>& pick,
    size_t columns
) -> void {
    for (size_t start = 0; start < colors.size(); start += columns) {
        std::vector<Cell> cells;
        for (size_t i = start; i < std::min(start + columns, colors.size()); ++i) {
            const auto& color = colors[i];
            const auto swatch = EditorColors::Hex(color);
            cells.push_back({
                .text = color == current ? "*" : "",
                .click = [pick, color]() -> void { pick(color); },
                .color = swatch,
                .text_color = EditorColors::Readable(swatch),
                .rim = true,
            });
        }
        while (cells.size() < columns) {
            cells.push_back({.text = ""});
        }
        Row(std::move(cells), 28.0f);
    }
}

auto PanelBuilder::Gap(float height) -> void {
    m_y += height;
}
