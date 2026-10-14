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
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "TextInput.hpp"
#include "VirtualKeys.hpp"

using TextCommit = std::function<void(const std::string&)>;

using KeysCaptured = std::function<void(const std::vector<VirtualKey>&)>;

class TextEntry final : public TextInput, public std::enable_shared_from_this<TextEntry>
{
  public:
    [[nodiscard]] auto Editing(std::string_view id) const -> bool { return m_id == id; }
    [[nodiscard]] auto Editing() const -> bool { return m_id.has_value(); }
    [[nodiscard]] auto Buffer() const -> const std::string& { return m_buffer; }
    [[nodiscard]] auto Capturing() const -> bool { return m_captured != nullptr; }
    [[nodiscard]] auto CapturingShortcut() const -> bool { return m_shortcut; }
    [[nodiscard]] auto Held() const -> const std::vector<VirtualKey>& { return m_held; }

    auto SetOnChange(std::function<void()> on_change) -> void { m_on_change = std::move(on_change); }

    auto Begin(std::string id, std::string initial, TextCommit commit) -> void;
    auto Capture(bool shortcut, KeysCaptured captured) -> void;
    auto Commit() -> void;
    auto End() -> void;

    [[nodiscard]] auto WantsRawKeys() const -> bool override { return Capturing(); }
    auto Key(VirtualKey key, uint8_t modifiers, std::string_view text) -> void override;

  private:
    auto Focus() -> void;
    auto Changed() -> void;

    std::optional<std::string> m_id;
    std::string m_buffer;
    TextCommit m_commit;
    bool m_shortcut = false;
    std::vector<VirtualKey> m_held;
    KeysCaptured m_captured;
    std::function<void()> m_on_change;
};
