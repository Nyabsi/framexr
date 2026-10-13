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
#include <span>
#include <string>

#include <Eigen/Core>

#include "FormComponent.hpp"

class Editor;

class StackComponent;

class ColorPanel final : public FormComponent {
  public:
    ColorPanel(Editor& editor, const Eigen::Vector2f& size);

  private:
    auto AddPicker(Editor& editor) -> void;
    auto AddSwatches(Editor& editor, std::span<const std::string> colors, size_t columns) -> void;
    auto AddHues(Editor& editor) -> void;
    auto AddFooter(Editor& editor) -> void;

    StackComponent& m_stack;
};
