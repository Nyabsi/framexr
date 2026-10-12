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

#include "ModularElements.hpp"

#include "ModularUiConfig.hpp"
#include "framexr_config.h"

auto ModularElements::Rect(const ModularElement& element) -> const std::array<float, 4>& {
    return std::visit([](const auto& item) -> const std::array<float, 4>& { return item.rect; }, element);
}

auto ModularElements::Rect(ModularElement& element) -> std::array<float, 4>& {
    return std::visit([](auto& item) -> std::array<float, 4>& { return item.rect; }, element);
}

auto ModularElements::RegionOf(const ModularElement& element) -> Region {
    return std::visit([](const auto& item) -> Region { return item.region; }, element);
}

auto ModularElements::SetRegion(ModularElement& element, Region region) -> void {
    std::visit([region](auto& item) -> void { item.region = region; }, element);
}

auto ModularElements::FormOf(const ModularElement& element) -> size_t {
    return std::visit([](const auto& item) -> size_t { return item.form; }, element);
}

auto ModularElements::SetForm(ModularElement& element, size_t form) -> void {
    std::visit([form](auto& item) -> void { item.form = form; }, element);
}

auto ModularElements::TypeName(const ModularElement& element) -> std::string_view {
    return std::visit(
        [](const auto& item) -> std::string_view {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, PanelElement>) {
                return "Panel";
            } else if constexpr (std::is_same_v<Item, LabelElement>) {
                return item.centered ? "CenteredLabel" : "Label";
            } else if constexpr (std::is_same_v<Item, SpriteElement>) {
                return "Sprite";
            } else if constexpr (std::is_same_v<Item, ButtonElement>) {
                return "Button";
            } else if constexpr (std::is_same_v<Item, BatteryListElement>) {
                return "BatteryList";
            } else {
                return "OverlayList";
            }
        },
        element
    );
}
