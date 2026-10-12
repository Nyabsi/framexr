// FrameXR
//
// This is a port of wlx-overlay-s from Rust to C++.
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
// Copyright (C) 2023-2025 galister and wlx-overlay-s contributors
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

#include "ModularUiConfig.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>

#include "Control.hpp"
#include "ModularElements.hpp"
#include "framexr_config.h"

namespace {

constexpr float kMinToolbarGap = 2.0f;

auto IsList(const ModularElement& element) -> bool {
    return std::holds_alternative<BatteryListElement>(element) || std::holds_alternative<OverlayListElement>(element);
}

} // namespace

auto ModularUiConfig::Size() const -> std::array<uint32_t, 2> {
    const Eigen::Vector2f origin = Origin();
    float right = origin.x() + m_form.width;
    float bottom = origin.y() + m_form.height;
    for (size_t index = 1; index < FormCount(); ++index) {
        const auto rect = FormRect(index, origin);
        right = std::max(right, rect.x + rect.w);
        bottom = std::max(bottom, rect.y + rect.h);
    }
    if (const auto toolbar = ToolbarRect(origin)) {
        right = std::max(right, toolbar->x + toolbar->w);
        bottom = std::max(bottom, toolbar->y + toolbar->h);
    }
    return {static_cast<uint32_t>(std::ceil(right)), static_cast<uint32_t>(std::ceil(bottom))};
}

auto ModularUiConfig::Origin() const -> Eigen::Vector2f {
    Eigen::Vector2f minimum = Eigen::Vector2f::Zero();
    for (const auto& extra : m_forms) {
        minimum = minimum.cwiseMin(Eigen::Vector2f(extra.x, extra.y));
    }
    if (m_form.toolbar && m_form.toolbar->position == ToolbarPosition::Above) {
        minimum.y() = std::min(minimum.y(), -(m_form.toolbar->height + m_form.toolbar->gap));
    }
    return -minimum;
}

auto ModularUiConfig::FormRect(size_t index) const -> CanvasRect {
    return FormRect(index, Origin());
}

auto ModularUiConfig::FormRect(size_t index, const Eigen::Vector2f& origin) const -> CanvasRect {
    if (index == 0 || index > m_forms.size()) {
        return {origin.x(), origin.y(), m_form.width, m_form.height};
    }
    const auto& extra = m_forms[index - 1];
    return {origin.x() + extra.x, origin.y() + extra.y, extra.width, extra.height};
}

auto ModularUiConfig::ToolbarRect() const -> std::optional<CanvasRect> {
    return ToolbarRect(Origin());
}

auto ModularUiConfig::ToolbarRect(const Eigen::Vector2f& origin) const -> std::optional<CanvasRect> {
    if (!m_form.toolbar) {
        return std::nullopt;
    }
    const auto& toolbar = *m_form.toolbar;
    const float top = toolbar.position == ToolbarPosition::Above ? -(toolbar.height + toolbar.gap) : m_form.height + toolbar.gap;
    return CanvasRect{origin.x(), origin.y() + top, m_form.width, toolbar.height};
}

auto ModularUiConfig::Backgrounds(const Eigen::Vector2f& origin) const -> std::vector<ModularElement> {
    std::vector<ModularElement> backgrounds;
    for (size_t index = 0; index < FormCount(); ++index) {
        const auto& color = index == 0 ? m_form.bg_color : m_forms[index - 1].bg_color;
        if (color) {
            const auto rect = FormRect(index, origin);
            backgrounds.emplace_back(PanelElement{.rect = {rect.x, rect.y, rect.w, rect.h}, .bg_color = *color});
        }
    }
    return backgrounds;
}

auto ModularUiConfig::RemoveForm(size_t index) -> void {
    if (index == 0 || index > m_forms.size()) {
        return;
    }
    m_forms.erase(m_forms.begin() + static_cast<std::ptrdiff_t>(index) - 1);
    std::erase_if(m_elements, [index](const ModularElement& element) -> bool {
        return ModularElements::RegionOf(element) == Region::Form && ModularElements::FormOf(element) == index;
    });
    for (auto& element : m_elements) {
        if (const auto form_index = ModularElements::FormOf(element); form_index > index) {
            ModularElements::SetForm(element, form_index - 1);
        }
    }
}

auto ModularUiConfig::Arrange() const -> std::vector<ModularElement> {
    return Arrange(Origin());
}

auto ModularUiConfig::Arrange(const Eigen::Vector2f& origin) const -> std::vector<ModularElement> {
    const auto toolbar_rect = ToolbarRect(origin);
    std::vector<ModularElement> arranged;
    std::vector<size_t> toolbar_items;
    for (const auto& element : m_elements) {
        auto& placed = arranged.emplace_back(element);
        auto& rect = ModularElements::Rect(placed);
        if (ModularElements::RegionOf(element) == Region::Toolbar) {
            toolbar_items.push_back(arranged.size() - 1);
        } else {
            const auto form_rect = FormRect(ModularElements::FormOf(element), origin);
            rect[0] += form_rect.x;
            rect[1] += form_rect.y;
        }
    }
    if (toolbar_items.empty()) {
        return arranged;
    }
    if (!toolbar_rect) {
        for (const auto index : std::views::reverse(toolbar_items)) {
            arranged.erase(arranged.begin() + static_cast<std::ptrdiff_t>(index));
        }
        return arranged;
    }

    const auto count = static_cast<float>(toolbar_items.size());
    float total = 0.0f;
    for (const auto index : toolbar_items) {
        total += ModularElements::Rect(arranged[index])[2];
    }
    float gap = (toolbar_rect->w - total) / (count + 1.0f);
    float stretch = 1.0f;
    if (gap < kMinToolbarGap && total > 0.0f) {
        gap = kMinToolbarGap;
        stretch = std::max(toolbar_rect->w - gap * (count + 1.0f), 0.0f) / total;
    }
    const float padding = m_form.toolbar ? m_form.toolbar->padding : 0.0f;
    float x = toolbar_rect->x + gap;
    for (const auto index : toolbar_items) {
        auto& rect = ModularElements::Rect(arranged[index]);
        const float item_width = rect[2] * stretch;
        if (IsList(arranged[index])) {
            rect = {x, toolbar_rect->y, item_width, toolbar_rect->h};
        } else {
            rect = {x, toolbar_rect->y + padding, item_width, std::max(toolbar_rect->h - 2.0f * padding, 1.0f)};
        }
        x += item_width + gap;
    }
    return arranged;
}
