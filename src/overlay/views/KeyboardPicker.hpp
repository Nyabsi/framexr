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

#include <memory>
#include <string_view>

#include "View.hpp"

class Keymap;

constexpr std::string_view kKeyboardPickerName = "kbd-picker";

class KeyboardPicker final : public View
{
  public:
    static auto Open(std::shared_ptr<const Keymap> keymap) -> void;

    auto Update() -> void override;

  private:
    KeyboardPicker() = default;

    static auto Create(const std::shared_ptr<const Keymap>& keymap) -> std::unique_ptr<KeyboardPicker>;
};
