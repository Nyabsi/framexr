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
#include <memory>
#include <string_view>

#include "TextInput.hpp"
#include "VirtualKey.hpp"

class Editor;

class EditorTextInput final : public TextInput {
  public:
    explicit EditorTextInput(std::weak_ptr<Editor> editor);

    [[nodiscard]] auto WantsRawKeys() const -> bool override;
    auto Key(VirtualKey key, uint8_t modifiers, std::string_view text) -> void override;

  private:
    std::weak_ptr<Editor> m_editor;
};
