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

#include <nlohmann/json.hpp>

#include "FormComponent.hpp"
#include "document/Common.hpp"

struct BatteryListElement {
    std::array<float, 4> rect{};
    std::optional<float> corner_radius;
    int font_size = 0;
    std::string fg_color;
    std::string fg_color_low;
    std::string fg_color_charging;
    float low_threshold = 0.0f;
    size_t num_devices = 0;
    ListLayout layout = ListLayout::Horizontal;
    Region region = Region::Form;
    size_t form = 0;
};

class BatteryListComponent final : public FormComponent {
  public:
    static constexpr std::string_view kType = "BatteryList";

    static auto Parse(const nlohmann::json& json) -> BatteryListElement;
    static auto Serialize(nlohmann::ordered_json& json, const BatteryListElement& element) -> void;
    static auto Create(FormComponent& parent, const BatteryListElement& element, const FormContext& context)
        -> BatteryListComponent&;

    explicit BatteryListComponent(std::string name = {});
};
