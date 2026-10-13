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

#include <string>
#include <string_view>

#include "FormComponent.hpp"
#include "TextEntry.hpp"

class ButtonComponent;

class TextLineComponent;

class LaserInputComponent final : public FormComponent {
  public:
    static constexpr float kCaptionHeight = 16.0f;
    static constexpr float kFieldHeight = 34.0f;
    static constexpr float kGap = 6.0f;
    static constexpr int kCaptionFont = 12;
    static constexpr int kFieldFont = 14;

    LaserInputComponent(std::string name, std::string_view caption, TextEntry& entry, std::string value, TextCommit commit);

  protected:
    auto Layout() -> void override;

  private:
    TextLineComponent& m_caption;
    ButtonComponent& m_field;
};
