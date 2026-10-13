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

#include "TextEntry.hpp"

#include <algorithm>
#include <utility>

#include "View.hpp"
#include "Interaction.hpp"
#include "TaskQueue.hpp"
#include "views/Keyboard.hpp"

namespace {

auto PopCodepoint(std::string& text) -> void {
    while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0U) == 0x80U) {
        text.pop_back();
    }
    if (!text.empty()) {
        text.pop_back();
    }
}

} // namespace

auto TextEntry::Begin(std::string id, std::string initial, TextCommit commit) -> void {
    if (m_id && *m_id != id) {
        auto previous = std::exchange(m_commit, nullptr);
        auto buffer = std::move(m_buffer);
        m_id.reset();
        previous(buffer);
    }
    m_id = std::move(id);
    m_buffer = std::move(initial);
    m_commit = std::move(commit);
    m_captured = nullptr;
    m_held.clear();
    Focus();
}

auto TextEntry::Capture(bool shortcut, KeysCaptured captured) -> void {
    m_id.reset();
    m_commit = nullptr;
    m_shortcut = shortcut;
    m_held.clear();
    m_captured = std::move(captured);
    Focus();
}

auto TextEntry::Commit() -> void {
    if (!m_id) {
        return;
    }
    auto commit = std::exchange(m_commit, nullptr);
    auto buffer = std::move(m_buffer);
    End();
    commit(buffer);
}

auto TextEntry::End() -> void {
    m_id.reset();
    m_buffer.clear();
    m_commit = nullptr;
    m_captured = nullptr;
    m_held.clear();
    if (g_interaction->FocusedTextInput().get() == this) {
        g_interaction->SetFocusedTextInput(nullptr);
    }
    Changed();
}

auto TextEntry::Key(VirtualKey key, uint8_t, std::string_view text) -> void {
    if (m_captured) {
        if (m_shortcut && VirtualKeys::ModifierOf(key) && !VirtualKeys::Latches(key)) {
            if (const auto found = std::ranges::find(m_held, key); found != m_held.end()) {
                m_held.erase(found);
            } else {
                m_held.push_back(key);
            }
            Changed();
            return;
        }
        auto keys = m_held;
        keys.push_back(key);
        auto captured = std::exchange(m_captured, nullptr);
        End();
        captured(keys);
        Changed();
        return;
    }
    if (!m_id) {
        return;
    }
    switch (key) {
    case VirtualKey::Return:
    case VirtualKey::KpEnter: Commit(); break;
    case VirtualKey::Escape: End(); break;
    case VirtualKey::BackSpace: PopCodepoint(m_buffer); break;
    default: m_buffer += text; break;
    }
    Changed();
}

auto TextEntry::Focus() -> void {
    g_interaction->SetFocusedTextInput(shared_from_this());
    g_tasks->Enqueue(
        ModifyOverlayTask{
            .selector = std::string(kKeyboardName),
            .modify = [](View& overlay) -> void {
                if (!overlay.WantVisible()) {
                    overlay.SetWantVisible(true);
                    overlay.Reset(false);
                }
            },
        }
    );
    Changed();
}

auto TextEntry::Changed() -> void {
    if (m_on_change) {
        m_on_change();
    }
}
