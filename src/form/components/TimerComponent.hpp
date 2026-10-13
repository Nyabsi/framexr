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

#include <nlohmann/json.hpp>

#include "FormComponent.hpp"
#include "LabelComponent.hpp"
#include "document/Common.hpp"

struct TimerElement {
    std::array<float, 4> rect{};
    int font_size = 0;
    std::string fg_color;
    bool centered = false;
    std::string format;
    Region region = Region::Form;
    size_t form = 0;
};

class TimerComponent final : public FormComponent {
  public:
    static constexpr std::string_view kType = "Timer";

    static auto Parse(const nlohmann::json& json) -> TimerElement;
    static auto Serialize(nlohmann::ordered_json& json, const TimerElement& element) -> void;
    static auto Create(FormComponent& parent, const TimerElement& element, const FormContext& context) -> TimerComponent&;

    TimerComponent(std::string name, std::string format);

    [[nodiscard]] auto Label() -> LabelComponent& { return m_label; }

    auto Update() -> void override;

  protected:
    auto Layout() -> void override;

  private:
    LabelComponent& m_label;
    std::string m_format;
    std::chrono::steady_clock::time_point m_start;
};
