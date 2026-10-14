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
#include <string>

#include <Eigen/Core>

#include "FormComponent.hpp"

class Editor;

class StackComponent;

class WatchProperties final : public FormComponent
{
  public:
    WatchProperties(Editor& editor, const Eigen::Vector2f& size);

  private:
    auto AddOverview(Editor& editor) -> void;
    auto AddForm(Editor& editor, size_t form) -> void;
    auto AddElement(Editor& editor, size_t index) -> void;
    auto AddText(Editor& editor, size_t index) -> void;
    auto AddTimezone(Editor& editor, size_t index, const std::string& zone) -> void;
    auto AddLook(Editor& editor, size_t index) -> void;
    auto AddButtonActions(Editor& editor) -> void;
    auto AddArrange(Editor& editor, size_t index) -> void;

    StackComponent& m_stack;
};
