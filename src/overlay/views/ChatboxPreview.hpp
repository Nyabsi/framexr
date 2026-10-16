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
#include <string>
#include <string_view>

#include "Error.hpp"
#include "View.hpp"

class Chatbox;

constexpr std::string_view kChatboxPreviewName = "kbd-chatbox";

class ChatboxPreview final : public View
{
  public:
    static auto Open(std::weak_ptr<const Chatbox> chatbox) -> void;

    auto Update() -> void override;

  private:
    explicit ChatboxPreview(std::weak_ptr<const Chatbox> chatbox);

    auto Show(const Chatbox& chatbox) -> Result<>;
    auto Follow(const View& keyboard) -> void;

    std::weak_ptr<const Chatbox> m_chatbox;
    std::string m_text;
};
