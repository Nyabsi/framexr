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
#include "document/Common.hpp"

struct LabelElement {
    std::array<float, 4> rect{};
    int font_size = 0;
    std::string fg_color;
    bool centered = false;
    std::string text;
    Region region = Region::Form;
    size_t form = 0;
};

class LabelComponent final : public FormComponent {
  public:
    static constexpr std::string_view kType = "Label";

    static auto Parse(const nlohmann::json& json) -> LabelElement;
    static auto Serialize(nlohmann::ordered_json& json, const LabelElement& element) -> void;
    static auto Create(FormComponent& parent, const LabelElement& element, const FormContext& context) -> LabelComponent&;

    explicit LabelComponent(std::string name = {}, std::string_view text = {});

    [[nodiscard]] auto Text() const -> const std::string& { return m_text; }
    [[nodiscard]] auto FontSize() const -> int { return m_font_size; }
    [[nodiscard]] auto FgColor() const -> const Eigen::Vector4f& { return m_fg_color; }
    [[nodiscard]] auto Centered() const -> bool { return m_centered; }

    auto SetText(std::string_view text) -> void;
    auto SetFontSize(int font_size) -> void;
    auto SetFgColor(const Eigen::Vector4f& fg_color) -> void;
    auto SetCentered(bool centered) -> void;

  protected:
    auto Emit(FormState& state, const FormFrame& frame) -> void override;

  private:
    std::string m_text;
    int m_font_size = 24;
    Eigen::Vector4f m_fg_color = Eigen::Vector4f::Ones();
    bool m_centered = false;
};
