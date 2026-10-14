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

#include <cstdint>
#include <functional>

#include "FormComponent.hpp"
#include "FormTheme.hpp"
#include "document/Actions.hpp"

enum class Region : uint8_t
{
    Form,
    Toolbar,
};

enum class ListLayout : uint8_t
{
    Horizontal,
    Vertical,
};

using ButtonBinder = std::function<void(FormComponent&, const ButtonData&)>;

struct FormContext
{
    const ThemeDefinition& theme;
    ButtonBinder bind;
};
