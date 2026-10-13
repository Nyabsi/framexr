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
#include <optional>
#include <string>
#include <variant>

#include <Eigen/Core>

#include "FormComponent.hpp"

class Editor;

class StackComponent;

class TimezonePanel final : public FormComponent {
  public:
    static auto ZoneName(const std::optional<std::variant<size_t, std::string>>& zone) -> std::string;
    static auto LocalZone() -> std::string;

    TimezonePanel(Editor& editor, const Eigen::Vector2f& size);

  private:
    StackComponent& m_stack;
};
