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

#include "EditorTextInput.hpp"

#include <utility>

#include "Editor.hpp"

EditorTextInput::EditorTextInput(std::weak_ptr<Editor> editor) : m_editor(std::move(editor)) {
}

auto EditorTextInput::WantsRawKeys() const -> bool {
    const auto editor = m_editor.lock();
    return editor && editor->Capturing();
}

auto EditorTextInput::Key(VirtualKey key, uint8_t, std::string_view text) -> void {
    if (const auto editor = m_editor.lock()) {
        editor->OnKey(key, text);
    }
}
