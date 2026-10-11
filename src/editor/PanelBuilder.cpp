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

#include "Font.hpp"
#include "State.hpp"
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

auto EditorColors::Hex(std::string_view hex) -> Eigen::Vector4f {
    return Canvas::ParseColor(hex).value_or(Canvas::FallbackColor());
}

auto EditorColors::Readable(const Eigen::Vector4f& background) -> Eigen::Vector4f {
    const float luminance = 0.2126f * background.x() + 0.7152f * background.y() + 0.0722f * background.z();
    return luminance > 0.5f ? Hex("#181926") : Hex("#eff1f5");
}

auto EditorColors::Background() -> Eigen::Vector4f {
    return Hex("@panel");
}

auto EditorColors::Surface() -> Eigen::Vector4f {
    return Hex("@surface");
}

auto EditorColors::Accent() -> Eigen::Vector4f {
    return Hex("@accent");
}

auto EditorColors::Text() -> Eigen::Vector4f {
    return Hex("@text");
}

auto EditorColors::Dim() -> Eigen::Vector4f {
    return Hex("@subtle");
}

auto EditorColors::Dark() -> Eigen::Vector4f {
    return Hex("@shadow");
}

auto EditorColors::OnAccent() -> Eigen::Vector4f {
    return Hex("@on-accent");
}

auto EditorColors::Danger() -> Eigen::Vector4f {
    return Hex("@red");
}

CallbackControl::CallbackControl(Click click, Click press, Scroll scroll)
    : click_(std::move(click)),
      press_(std::move(press)),
      scroll_(std::move(scroll)) {
}

auto CallbackControl::OnPress(AppState& app, PointerMode) -> void {
    if (press_) {
        press_(app);
    }
}

auto CallbackControl::OnRelease(AppState& app) -> void {
    if (click_) {
        click_(app);
    }
}

auto CallbackControl::OnScroll(AppState& app, float delta_y, float) -> void {
    if (scroll_) {
        scroll_(app, delta_y);
    }
}

OutlineControl::OutlineControl(std::optional<Eigen::Vector4f> fill, std::optional<HighlightOutline> outline)
    : fill_(std::move(fill)),
      outline_(std::move(outline)) {
}

auto OutlineControl::HighlightColor(AppState&) -> std::optional<Eigen::Vector4f> {
    return fill_;
}

auto OutlineControl::Outline(AppState&) -> std::optional<HighlightOutline> {
    return outline_;
}

PanelBuilder::PanelBuilder(Canvas& canvas, AppState& app)
    : canvas_(canvas),
      app_(app),
      width_(static_cast<float>(canvas.Width())),
      radius_(Theme::Radius(Theme::Active().rounding, Shape::Control)) {
    canvas_.bg_color = EditorColors::Background();
    canvas_.Panel(0.0f, 0.0f, width_, static_cast<float>(canvas.Height()), Theme::Radius(Theme::Active().rounding, Shape::Panel));
}

auto PanelBuilder::TextWidth(std::string_view text, int font_size) const -> float {
    if (app_.fonts && app_.device) {
        if (const auto size = app_.fonts->MeasureText(text, font_size, *app_.device)) {
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
    canvas_.font_size = font_size;
    canvas_.fg_color = color;
    auto shown = Ellipsize(text, font_size, std::max(w - (centered ? 12.0f : 0.0f), 8.0f));
    if (centered) {
        canvas_.LabelCentered(x, y, w, h, 0.0f, std::move(shown));
    } else {
        canvas_.Label(x, y + static_cast<float>(font_size), w, h, 0.0f, std::move(shown));
    }
}

auto PanelBuilder::Rim(float x, float y, float w, float h) -> void {
    canvas_.Decoration(
        x,
        y,
        w,
        h,
        radius_,
        std::make_unique<OutlineControl>(
            std::nullopt,
            HighlightOutline{.color = WithAlpha(EditorColors::Dim(), 0.35f), .width = 1.0f}
        )
    );
}

auto PanelBuilder::Title(std::string_view text) -> void {
    Label(kPadding, y_, width_ - 2.0f * kPadding, 24.0f, text, kTitleFont, EditorColors::Text(), false);
    y_ += 24.0f + kGap;
}

auto PanelBuilder::Section(std::string_view text) -> void {
    y_ += kGap;
    canvas_.bg_color = WithAlpha(EditorColors::Dim(), 0.25f);
    canvas_.Panel(kPadding, y_, width_ - 2.0f * kPadding, 1.0f, 0.0f);
    y_ += kGap;
    Label(kPadding, y_, width_ - 2.0f * kPadding, 18.0f, text, kSectionFont, EditorColors::Accent(), false);
    y_ += 18.0f + kGap;
}

auto PanelBuilder::Caption(std::string_view text) -> void {
    Label(kPadding, y_, width_ - 2.0f * kPadding, 16.0f, text, kCaptionFont, EditorColors::Dim(), false);
    y_ += 16.0f + kGap;
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
    const float available = width_ - 2.0f * kPadding - kGap * static_cast<float>(cells.size() - 1);
    float x = kPadding;
    for (auto& cell : cells) {
        const float w = available * cell.weight / total;
        const bool interactive = cell.click || cell.press || cell.scroll;
        const auto bg = cell.selected ? EditorColors::Accent() : cell.color.value_or(EditorColors::Surface());
        const auto fg = cell.text_color.value_or(cell.selected ? EditorColors::OnAccent() : EditorColors::Text());
        canvas_.font_size = kTextFont;
        canvas_.fg_color = fg;
        canvas_.bg_color = bg;
        if (interactive) {
            canvas_.Button(
                x,
                y_,
                w,
                height,
                radius_,
                Ellipsize(cell.text, kTextFont, std::max(w - 12.0f, 8.0f)),
                std::make_unique<CallbackControl>(std::move(cell.click), std::move(cell.press), std::move(cell.scroll))
            );
        } else {
            if (cell.color || cell.selected) {
                canvas_.Panel(x, y_, w, height, radius_);
            }
            Label(x, y_, w, height, cell.text, kTextFont, fg, cell.color.has_value() || cell.selected);
        }
        if (cell.rim) {
            Rim(x, y_, w, height);
        }
        x += w + kGap;
    }
    y_ += height + kGap;
}

auto PanelBuilder::Button(Cell cell) -> void {
    std::vector<Cell> cells;
    cells.push_back(std::move(cell));
    Row(std::move(cells));
}

auto PanelBuilder::Stepper(std::string_view label, std::string_view value, Click minus, Click plus, Scroll scroll) -> void {
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
    const std::function<void(AppState&, const std::string&)>& pick,
    size_t columns
) -> void {
    for (size_t start = 0; start < colors.size(); start += columns) {
        std::vector<Cell> cells;
        for (size_t i = start; i < std::min(start + columns, colors.size()); ++i) {
            const auto& color = colors[i];
            const auto swatch = EditorColors::Hex(color);
            cells.push_back({
                .text = color == current ? "*" : "",
                .click = [pick, color](AppState& app) -> void { pick(app, color); },
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
    y_ += height;
}
