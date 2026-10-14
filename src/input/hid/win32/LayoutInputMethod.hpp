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

#include "InputMethod.hpp"

class LayoutInputMethod final : public InputMethod
{
  public:
    LayoutInputMethod() = default;
    ~LayoutInputMethod() override;
    LayoutInputMethod(const LayoutInputMethod&) = delete;
    auto operator=(const LayoutInputMethod&) -> LayoutInputMethod& = delete;

    auto Select(ImeScript script) -> void override;

  private:
    ImeScript m_active = ImeScript::None;
    void* m_previous = nullptr;
    void* m_loaded = nullptr;
};
