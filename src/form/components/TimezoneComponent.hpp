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
#include <string>
#include <string_view>
#include <variant>

#include <nlohmann/json.hpp>

#include "FormComponent.hpp"
#include "LabelComponent.hpp"
#include "document/Common.hpp"

struct TimezoneElement
{
    std::array<float, 4> rect{};
    int font_size = 0;
    std::string fg_color;
    bool centered = false;
    std::variant<size_t, std::string> timezone = size_t{0};
    Region region = Region::Form;
    size_t form = 0;
};

class TimezoneComponent final : public FormComponent
{
  public:
    static constexpr std::string_view kType = "Timezone";

    static auto Parse(const nlohmann::json& json) -> TimezoneElement;
    static auto Serialize(nlohmann::ordered_json& json, const TimezoneElement& element) -> void;
    static auto Create(FormComponent& parent, const TimezoneElement& element, const FormContext& context) -> TimezoneComponent&;

    explicit TimezoneComponent(std::string name = {}, std::string_view zone = {});

    [[nodiscard]] auto Label() -> LabelComponent& { return m_label; }

  protected:
    auto Layout() -> void override;

  private:
    LabelComponent& m_label;
};
