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
#include <chrono>
#include <optional>
#include <variant>

#include <nlohmann/json.hpp>

#include "FormComponent.hpp"
#include "LabelComponent.hpp"
#include "document/Common.hpp"

struct ClockElement {
    std::array<float, 4> rect{};
    int font_size = 0;
    std::string fg_color;
    bool centered = false;
    std::string format;
    std::optional<std::variant<size_t, std::string>> timezone;
    std::optional<int> city_size;
    Region region = Region::Form;
    size_t form = 0;
};

class ClockComponent final : public FormComponent {
  public:
    static constexpr std::string_view kType = "Clock";

    static auto Parse(const nlohmann::json& json) -> ClockElement;
    static auto Serialize(nlohmann::ordered_json& json, const ClockElement& element) -> void;
    static auto Create(FormComponent& parent, const ClockElement& element, const FormContext& context) -> ClockComponent&;

    ClockComponent(std::string name, std::string format, const std::chrono::time_zone* zone);

    [[nodiscard]] auto Label() -> LabelComponent& { return m_label; }
    static auto Zone(std::string_view name) -> const std::chrono::time_zone*;
    static auto ZoneName(const std::variant<size_t, std::string>& zone) -> std::optional<std::string>;
    static auto City(const std::optional<std::variant<size_t, std::string>>& zone) -> std::string;
    static auto FormatTime(std::string_view format, const std::chrono::time_zone* zone, std::chrono::system_clock::time_point now)
        -> std::string;

    auto SetCity(std::string_view city, int font_size) -> void;

    auto Update() -> void override;

  protected:
    auto Layout() -> void override;

  private:
    LabelComponent& m_label;
    LabelComponent* m_city = nullptr;
    std::string m_format;
    const std::chrono::time_zone* m_zone;
};
