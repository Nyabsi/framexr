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

struct ButtonElement
{
    std::array<float, 4> rect{};
    std::optional<float> corner_radius;
    int font_size = 0;
    std::string fg_color;
    std::string bg_color;
    std::string text;
    ButtonData data;
    Region region = Region::Form;
    size_t form = 0;
};

class ButtonComponent final : public FormComponent
{
  public:
    static constexpr std::string_view kType = "Button";

    static auto Parse(const nlohmann::json& json) -> ButtonElement;
    static auto Serialize(nlohmann::ordered_json& json, const ButtonElement& element) -> void;
    static auto Create(FormComponent& parent, const ButtonElement& element, const FormContext& context) -> ButtonComponent&;

    explicit ButtonComponent(std::string name = {}, std::string_view text = {});

    [[nodiscard]] auto Text() const -> const std::string& { return m_text; }
    [[nodiscard]] auto FontSize() const -> int { return m_font_size; }
    [[nodiscard]] auto FgColor() const -> const Eigen::Vector4f& { return m_fg_color; }
    [[nodiscard]] auto BgColor() const -> const Eigen::Vector4f& { return m_bg_color; }
    [[nodiscard]] auto CornerRadius() const -> const std::optional<float>& { return m_corner_radius; }

    auto SetText(std::string_view text) -> void;
    auto SetFontSize(int font_size) -> void;
    auto SetFgColor(const Eigen::Vector4f& fg_color) -> void;
    auto SetBgColor(const Eigen::Vector4f& bg_color) -> void;
    auto SetCornerRadius(std::optional<float> corner_radius) -> void;
    auto SetEllipsize(bool ellipsize) -> void;

  protected:
    auto Emit(FormState& state, const FormFrame& frame) -> void override;

  private:
    std::string m_text;
    int m_font_size = 24;
    Eigen::Vector4f m_fg_color = Eigen::Vector4f::Ones();
    Eigen::Vector4f m_bg_color = Eigen::Vector4f::Zero();
    std::optional<float> m_corner_radius;
    bool m_ellipsize = false;
};
