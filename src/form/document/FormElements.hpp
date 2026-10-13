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

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <nlohmann/json.hpp>

#include "BatteryComponent.hpp"
#include "BatteryListComponent.hpp"
#include "ButtonComponent.hpp"
#include "ClockComponent.hpp"
#include "DragMultiplierComponent.hpp"
#include "ExecComponent.hpp"
#include "IpdComponent.hpp"
#include "LabelComponent.hpp"
#include "OverlayListComponent.hpp"
#include "PanelComponent.hpp"
#include "SpriteComponent.hpp"
#include "TimerComponent.hpp"
#include "TimezoneComponent.hpp"
#include "document/Common.hpp"

using FormElement = std::variant<
    PanelElement,
    LabelElement,
    ClockElement,
    TimezoneElement,
    TimerElement,
    BatteryElement,
    ExecElement,
    IpdElement,
    DragMultiplierElement,
    SpriteElement,
    ButtonElement,
    BatteryListElement,
    OverlayListElement>;

class FormElements final {
  public:
    static auto Rect(const FormElement& element) -> const std::array<float, 4>&;
    static auto Rect(FormElement& element) -> std::array<float, 4>&;
    static auto RegionOf(const FormElement& element) -> Region;
    static auto SetRegion(FormElement& element, Region region) -> void;
    static auto FormOf(const FormElement& element) -> size_t;
    static auto SetForm(FormElement& element, size_t form) -> void;
    static auto TypeName(const FormElement& element) -> std::string_view;
    static auto IsList(const FormElement& element) -> bool;
    static auto IsText(const FormElement& element) -> bool;
    static auto Centered(const FormElement& element) -> bool;
    static auto ToggleCentered(FormElement& element) -> void;
    static auto FontSize(const FormElement& element) -> std::optional<int>;
    static auto SetFontSize(FormElement& element, int font_size) -> void;
    static auto Color(const FormElement& element, bool background) -> std::optional<std::string>;
    static auto SetColor(FormElement& element, bool background, const std::string& color) -> void;
    static auto Radius(const FormElement& element) -> std::optional<std::optional<float>>;
    static auto SetRadius(FormElement& element, std::optional<float> radius) -> void;
    static auto Create(FormComponent& parent, const FormElement& element, const FormContext& context) -> void;
};

auto from_json(const nlohmann::json& json, FormElement& value) -> void;

auto to_json(nlohmann::ordered_json& json, const FormElement& value) -> void;
