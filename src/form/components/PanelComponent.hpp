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

#include <Eigen/Core>
#include <nlohmann/json.hpp>

#include "FormComponent.hpp"
#include "document/Common.hpp"

struct PanelElement
{
    std::array<float, 4> rect{};
    std::optional<float> corner_radius;
    std::string bg_color;
    Region region = Region::Form;
    size_t form = 0;
};

class PanelComponent final : public FormComponent
{
  public:
    static constexpr std::string_view kType = "Panel";

    static auto Parse(const nlohmann::json& json) -> PanelElement;
    static auto Serialize(nlohmann::ordered_json& json, const PanelElement& element) -> void;
    static auto Create(FormComponent& parent, const PanelElement& element, const FormContext& context) -> PanelComponent&;

    explicit PanelComponent(std::string name = {});

    [[nodiscard]] auto BgColor() const -> const Eigen::Vector4f& { return m_bg_color; }
    [[nodiscard]] auto CornerRadius() const -> const std::optional<float>& { return m_corner_radius; }

    auto SetBgColor(const Eigen::Vector4f& bg_color) -> void;
    auto SetCornerRadius(std::optional<float> corner_radius) -> void;
    auto SetThemeShape(Shape shape) -> void;

  protected:
    auto Emit(FormState& state, const FormFrame& frame) -> void override;

  private:
    Eigen::Vector4f m_bg_color = Eigen::Vector4f::Zero();
    std::optional<float> m_corner_radius;
    Shape m_shape = Shape::Panel;
};
