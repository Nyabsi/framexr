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

#include <Eigen/Core>

#include <nlohmann/json.hpp>

#include "FormComponent.hpp"
#include "LabelComponent.hpp"
#include "document/Common.hpp"

struct BatteryElement
{
    std::array<float, 4> rect{};
    int font_size = 0;
    std::string fg_color;
    bool centered = false;
    size_t device = 0;
    float low_threshold = 0.0f;
    std::string low_color;
    std::string charging_color;
    Region region = Region::Form;
    size_t form = 0;
};

class BatteryComponent final : public FormComponent
{
  public:
    static constexpr std::string_view kType = "Battery";

    static auto Parse(const nlohmann::json& json) -> BatteryElement;
    static auto Serialize(nlohmann::ordered_json& json, const BatteryElement& element) -> void;
    static auto Create(FormComponent& parent, const BatteryElement& element, const FormContext& context) -> BatteryComponent&;

    BatteryComponent(std::string name, size_t device, float low_threshold);

    [[nodiscard]] auto Label() -> LabelComponent& { return m_label; }
    auto SetColors(const Eigen::Vector4f& normal, const Eigen::Vector4f& low, const Eigen::Vector4f& charging) -> void;

    auto Update() -> void override;

  protected:
    auto Layout() -> void override;

  private:
    LabelComponent& m_label;
    size_t m_device;
    float m_low_threshold;
    Eigen::Vector4f m_normal_color = Eigen::Vector4f::Ones();
    Eigen::Vector4f m_low_color = Eigen::Vector4f::Ones();
    Eigen::Vector4f m_charging_color = Eigen::Vector4f::Ones();
};
