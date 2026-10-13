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

#include <span>
#include <string>
#include <string_view>

#include <Eigen/Core>

#include "FormComponent.hpp"
#include "VirtualKeys.hpp"

class Editor;

class StackComponent;

struct KeyGroup {
    std::string_view name;
    std::span<const VirtualKey> keys;
};

class KeyPickerPanel final : public FormComponent {
  public:
    static auto Caption(VirtualKey key) -> std::string;

    KeyPickerPanel(Editor& editor, const Eigen::Vector2f& size);

  private:
    StackComponent& m_stack;
};
