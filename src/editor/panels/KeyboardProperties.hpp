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

#include <Eigen/Core>

#include "FormComponent.hpp"

class Editor;

class StackComponent;

class KeyboardProperties final : public FormComponent
{
  public:
    KeyboardProperties(Editor& editor, const Eigen::Vector2f& size);

  private:
    auto AddKeyboards(Editor& editor) -> void;
    auto AddSettings(Editor& editor) -> void;
    auto AddKeyAction(Editor& editor, size_t index) -> void;
    auto AddKeyLabel(Editor& editor, size_t index) -> void;
    auto AddKeyLook(Editor& editor, size_t index) -> void;
    auto AddKeyArrange(Editor& editor, size_t index) -> void;

    StackComponent& m_stack;
};
