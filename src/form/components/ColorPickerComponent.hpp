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
#include <memory>
#include <string>

#include "FormComponent.hpp"

class Image;

class OutlineComponent;

class SpriteComponent;

class ColorPickerComponent final : public FormComponent
{
  public:
    static constexpr float kWheelSize = 180.0f;
    static constexpr float kBarHeight = 16.0f;
    static constexpr float kGap = 6.0f;

    static auto Hex(float hue, float saturation, float value) -> std::string;
    static auto Hsv(const std::string& hex) -> std::array<float, 3>;
    static auto WheelImage(float value) -> std::shared_ptr<Image>;
    static auto BarImage(float hue, float saturation) -> std::shared_ptr<Image>;
    static auto WheelRect(float width) -> CanvasRect;
    static auto BarRect(float width) -> CanvasRect;

    ColorPickerComponent(
        std::string name,
        std::array<float, 3> hsv,
        const std::shared_ptr<Image>& wheel,
        const std::shared_ptr<Image>& bar
    );

  protected:
    auto Layout() -> void override;

  private:
    std::array<float, 3> m_hsv;
    SpriteComponent& m_wheel;
    OutlineComponent& m_marker;
    SpriteComponent& m_bar;
    OutlineComponent& m_knob;
};
