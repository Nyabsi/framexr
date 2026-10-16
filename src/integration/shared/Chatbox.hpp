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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ImeContext.hpp"
#include "ImeScript.hpp"
#include "Osc.hpp"
#include "TextInput.hpp"

struct QueuedKey
{
    VirtualKey key;
    uint8_t modifiers;
    std::string text;
};

class Chatbox final : public TextInput, public std::enable_shared_from_this<Chatbox>
{
  public:
    static constexpr size_t kMaxLength = 144;

    [[nodiscard]] auto Active() const -> bool;
    [[nodiscard]] auto Draft() const -> const std::string& { return m_draft; }
    [[nodiscard]] auto Display() const -> std::string;

    auto SetScript(ImeScript script) -> void;

    auto Toggle() -> void;
    auto Update(bool visible) -> void;

    [[nodiscard]] auto WantsRawKeys() const -> bool override { return false; }
    auto Key(VirtualKey key, uint8_t modifiers, std::string_view text) -> void override;

  private:
    auto Open() -> void;
    auto Close() -> void;
    auto Attach() -> void;
    auto Connect() -> void;
    auto Edit(uint32_t keysym, std::string_view text) -> void;
    auto Send(std::string text) -> void;
    auto Deliver(const std::string& address, const std::vector<OscValue>& values) -> void;
    auto Changed() -> void;

    std::string m_draft;
    std::unique_ptr<ImeContext> m_ime;
    std::optional<std::chrono::steady_clock::time_point> m_connecting;
    std::vector<QueuedKey> m_queued;
    ImeScript m_script = ImeScript::None;
    bool m_open = false;
    bool m_typing = false;
    bool m_warned = false;
};
