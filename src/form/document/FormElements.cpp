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

#include "document/FormElements.hpp"

#include <algorithm>
#include <type_traits>

auto FormElements::Rect(const FormElement& element) -> const std::array<float, 4>&
{
    return std::visit([](const auto& item) -> const std::array<float, 4>& { return item.rect; }, element);
}

auto FormElements::Rect(FormElement& element) -> std::array<float, 4>&
{
    return std::visit([](auto& item) -> std::array<float, 4>& { return item.rect; }, element);
}

auto FormElements::RegionOf(const FormElement& element) -> Region
{
    return std::visit([](const auto& item) -> Region { return item.region; }, element);
}

auto FormElements::SetRegion(FormElement& element, Region region) -> void
{
    std::visit([region](auto& item) -> void { item.region = region; }, element);
}

auto FormElements::FormOf(const FormElement& element) -> size_t
{
    return std::visit([](const auto& item) -> size_t { return item.form; }, element);
}

auto FormElements::SetForm(FormElement& element, size_t form) -> void
{
    std::visit([form](auto& item) -> void { item.form = form; }, element);
}

auto FormElements::TypeName(const FormElement& element) -> std::string_view
{
    return std::visit(
        [](const auto& item) -> std::string_view
        {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, PanelElement>)
            {
                return PanelComponent::kType;
            }
            else if constexpr (std::is_same_v<Item, LabelElement>)
            {
                return LabelComponent::kType;
            }
            else if constexpr (std::is_same_v<Item, ClockElement>)
            {
                return ClockComponent::kType;
            }
            else if constexpr (std::is_same_v<Item, TimezoneElement>)
            {
                return TimezoneComponent::kType;
            }
            else if constexpr (std::is_same_v<Item, TimerElement>)
            {
                return TimerComponent::kType;
            }
            else if constexpr (std::is_same_v<Item, BatteryElement>)
            {
                return BatteryComponent::kType;
            }
            else if constexpr (std::is_same_v<Item, ExecElement>)
            {
                return ExecComponent::kType;
            }
            else if constexpr (std::is_same_v<Item, IpdElement>)
            {
                return IpdComponent::kType;
            }
            else if constexpr (std::is_same_v<Item, DragMultiplierElement>)
            {
                return DragMultiplierComponent::kType;
            }
            else if constexpr (std::is_same_v<Item, SpriteElement>)
            {
                return SpriteComponent::kType;
            }
            else if constexpr (std::is_same_v<Item, ButtonElement>)
            {
                return ButtonComponent::kType;
            }
            else if constexpr (std::is_same_v<Item, BatteryListElement>)
            {
                return BatteryListComponent::kType;
            }
            else
            {
                return OverlayListComponent::kType;
            }
        },
        element
    );
}

auto FormElements::IsList(const FormElement& element) -> bool
{
    return std::holds_alternative<BatteryListElement>(element) || std::holds_alternative<OverlayListElement>(element);
}

auto FormElements::IsText(const FormElement& element) -> bool
{
    return std::visit([](const auto& item) -> bool { return requires { item.centered; }; }, element);
}

auto FormElements::Centered(const FormElement& element) -> bool
{
    return std::visit(
        [](const auto& item) -> bool
        {
            if constexpr (requires { item.centered; })
            {
                return item.centered;
            }
            else
            {
                return false;
            }
        },
        element
    );
}

auto FormElements::Create(FormComponent& parent, const FormElement& element, const FormContext& context) -> void
{
    std::visit(
        [&](const auto& item) -> void
        {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, PanelElement>)
            {
                PanelComponent::Create(parent, item, context);
            }
            else if constexpr (std::is_same_v<Item, LabelElement>)
            {
                LabelComponent::Create(parent, item, context);
            }
            else if constexpr (std::is_same_v<Item, ClockElement>)
            {
                ClockComponent::Create(parent, item, context);
            }
            else if constexpr (std::is_same_v<Item, TimezoneElement>)
            {
                TimezoneComponent::Create(parent, item, context);
            }
            else if constexpr (std::is_same_v<Item, TimerElement>)
            {
                TimerComponent::Create(parent, item, context);
            }
            else if constexpr (std::is_same_v<Item, BatteryElement>)
            {
                BatteryComponent::Create(parent, item, context);
            }
            else if constexpr (std::is_same_v<Item, ExecElement>)
            {
                ExecComponent::Create(parent, item, context);
            }
            else if constexpr (std::is_same_v<Item, IpdElement>)
            {
                IpdComponent::Create(parent, item, context);
            }
            else if constexpr (std::is_same_v<Item, DragMultiplierElement>)
            {
                DragMultiplierComponent::Create(parent, item, context);
            }
            else if constexpr (std::is_same_v<Item, SpriteElement>)
            {
                SpriteComponent::Create(parent, item, context);
            }
            else if constexpr (std::is_same_v<Item, ButtonElement>)
            {
                ButtonComponent::Create(parent, item, context);
            }
            else if constexpr (std::is_same_v<Item, BatteryListElement>)
            {
                BatteryListComponent::Create(parent, item, context);
            }
            else
            {
                OverlayListComponent::Create(parent, item, context);
            }
        },
        element
    );
}

auto FormElements::ToggleCentered(FormElement& element) -> void
{
    std::visit(
        [](auto& item) -> void
        {
            if constexpr (requires { item.centered; })
            {
                const float font = static_cast<float>(item.font_size);
                item.rect[1] += item.centered ? font * 0.75f + item.rect[3] / 2.0f : -(font * 0.75f + item.rect[3] / 2.0f);
                item.centered = !item.centered;
            }
        },
        element
    );
}

auto FormElements::FontSize(const FormElement& element) -> std::optional<int>
{
    return std::visit(
        [](const auto& item) -> std::optional<int>
        {
            if constexpr (requires { item.font_size; })
            {
                return item.font_size;
            }
            else
            {
                return std::nullopt;
            }
        },
        element
    );
}

auto FormElements::SetFontSize(FormElement& element, int font_size) -> void
{
    std::visit(
        [font_size](auto& item) -> void
        {
            if constexpr (requires { item.font_size; })
            {
                item.font_size = std::clamp(font_size, 6, 200);
            }
        },
        element
    );
}

auto FormElements::Color(const FormElement& element, bool background) -> std::optional<std::string>
{
    return std::visit(
        [background](const auto& item) -> std::optional<std::string>
        {
            if constexpr (requires { item.bg_color; })
            {
                if (background)
                {
                    return item.bg_color;
                }
            }
            if constexpr (requires { item.fg_color; })
            {
                if (!background)
                {
                    return item.fg_color;
                }
            }
            return std::nullopt;
        },
        element
    );
}

auto FormElements::SetColor(FormElement& element, bool background, const std::string& color) -> void
{
    std::visit(
        [background, &color](auto& item) -> void
        {
            if constexpr (requires { item.bg_color; })
            {
                if (background)
                {
                    item.bg_color = color;
                }
            }
            if constexpr (requires { item.fg_color; })
            {
                if (!background)
                {
                    item.fg_color = color;
                }
            }
        },
        element
    );
}

auto FormElements::Radius(const FormElement& element) -> std::optional<std::optional<float>>
{
    return std::visit(
        [](const auto& item) -> std::optional<std::optional<float>>
        {
            if constexpr (requires { item.corner_radius; })
            {
                return item.corner_radius;
            }
            else
            {
                return std::nullopt;
            }
        },
        element
    );
}

auto FormElements::SetRadius(FormElement& element, std::optional<float> radius) -> void
{
    std::visit(
        [radius](auto& item) -> void
        {
            if constexpr (requires { item.corner_radius; })
            {
                item.corner_radius = radius;
            }
        },
        element
    );
}
