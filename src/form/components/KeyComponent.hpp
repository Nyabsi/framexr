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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <Eigen/Core>

#include "FormComponent.hpp"
#include "LabelComponent.hpp"

enum class KeyCapType : uint8_t
{
    Regular,
    RegularAltGr,
    Reversed,
    ReversedAltGr,
};

struct KeyCapLegend
{
    bool centered = false;
    CanvasRect rect;
};

struct KeyEmphasis
{
    bool shifted = false;
    bool altgr = false;
    bool native = false;

    auto operator==(const KeyEmphasis&) const -> bool = default;
};

class KeyComponent final : public FormComponent
{
  public:
    explicit KeyComponent(std::string name = {});

    [[nodiscard]] auto Legends() const -> const std::vector<LabelComponent*>& { return m_legends; }
    [[nodiscard]] auto Interactive() const -> bool { return m_interactive; }

    auto SetBgColor(const Eigen::Vector4f& bg_color) -> void;
    auto SetFgColor(const Eigen::Vector4f& fg_color) -> void;
    auto SetFontSize(int font_size) -> void;
    auto SetCornerRadius(std::optional<float> corner_radius) -> void;
    auto SetInteractive(bool interactive) -> void;
    auto SetLegends(KeyCapType cap_type, std::span<const std::string> labels, std::span<const std::string> ime = {}) -> void;
    auto Emphasize(const KeyEmphasis& emphasis) -> void;

  protected:
    auto Emit(FormState& state, const FormFrame& frame) -> void override;

  private:
    Eigen::Vector4f m_bg_color = Eigen::Vector4f::Zero();
    Eigen::Vector4f m_fg_color = Eigen::Vector4f::Ones();
    int m_font_size = 24;
    std::optional<float> m_corner_radius;
    bool m_interactive = true;
    std::vector<LabelComponent*> m_legends;
    KeyCapType m_cap_type = KeyCapType::Regular;
    size_t m_typed = 0;
};
