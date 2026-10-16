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

#include "Chatbox.hpp"

#include <format>
#include <span>
#include <utility>

#include "Interaction.hpp"
#include "FontCache.hpp"
#include "Logger.hpp"
#include "Toast.hpp"

namespace
{

constexpr auto kConnectTimeout = std::chrono::seconds(5);
constexpr uint32_t kReturn = 0xff0d;
constexpr uint32_t kKpEnter = 0xff8d;
constexpr uint32_t kBackSpace = 0xff08;
constexpr uint32_t kEscape = 0xff1b;
constexpr uint32_t kUnicodeKeysym = 0x01000000;

struct SpecialKeysym
{
    VirtualKey key;
    uint32_t keysym;
};

constexpr SpecialKeysym kSpecialKeysyms[] = {
    {VirtualKey::Return, kReturn},
    {VirtualKey::KpEnter, kKpEnter},
    {VirtualKey::BackSpace, kBackSpace},
    {VirtualKey::Escape, kEscape},
    {VirtualKey::Tab, 0xff09},
    {VirtualKey::Space, 0x20},
    {VirtualKey::Left, 0xff51},
    {VirtualKey::Up, 0xff52},
    {VirtualKey::Right, 0xff53},
    {VirtualKey::Down, 0xff54},
    {VirtualKey::Home, 0xff50},
    {VirtualKey::End, 0xff57},
    {VirtualKey::Prior, 0xff55},
    {VirtualKey::Next, 0xff56},
    {VirtualKey::Delete, 0xffff},
};

auto Keysym(VirtualKey key, std::string_view text) -> uint32_t
{
    for (const auto& special : kSpecialKeysyms)
    {
        if (special.key == key)
        {
            return special.keysym;
        }
    }
    const auto codepoints = FontCache::DecodeUtf8(text);
    if (codepoints.empty())
    {
        return 0;
    }
    return codepoints.front() < 0x100 ? codepoints.front() : (kUnicodeKeysym | codepoints.front());
}

auto KeysymText(uint32_t keysym) -> std::string
{
    if ((keysym & 0xff000000U) == kUnicodeKeysym)
    {
        keysym &= 0x00ffffffU;
    }
    else if (keysym < 0x20 || keysym >= 0x100)
    {
        return {};
    }
    return FontCache::EncodeUtf8(std::span(&keysym, 1));
}

auto PopCodepoint(std::string& text) -> void
{
    while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0U) == 0x80U)
    {
        text.pop_back();
    }
    if (!text.empty())
    {
        text.pop_back();
    }
}

auto Truncate(std::string& text, size_t codepoints) -> void
{
    size_t count = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if ((static_cast<unsigned char>(text[i]) & 0xC0U) != 0x80U && count++ == codepoints)
        {
            text.resize(i);
            return;
        }
    }
}

} // namespace

auto Chatbox::Active() const -> bool
{
    return m_open && g_interaction->FocusedTextInput().get() == this;
}

auto Chatbox::Toggle() -> void
{
    if (Active())
    {
        Close();
    }
    else
    {
        Open();
    }
}

auto Chatbox::Update(bool visible) -> void
{
    if (m_open && (!visible || !Active()))
    {
        Close();
    }
    if (m_connecting)
    {
        Connect();
    }
    if (m_ime)
    {
        m_ime->Poll();
    }
}

auto Chatbox::Display() const -> std::string
{
    if (m_connecting)
    {
        return m_draft + "\u2026";
    }
    if (!m_ime)
    {
        return m_draft;
    }
    std::string display = m_draft + m_ime->Preedit();
    for (size_t index = 0; index < m_ime->Candidates().size(); ++index)
    {
        display += std::format("{}{}.{}", index == 0 ? "  " : " ", index + 1, m_ime->Candidates()[index]);
    }
    return display;
}

auto Chatbox::SetScript(ImeScript script) -> void
{
    m_script = script;
    if (m_open)
    {
        Attach();
        Changed();
    }
}

auto Chatbox::Key(VirtualKey key, uint8_t modifiers, std::string_view text) -> void
{
    if (m_connecting)
    {
        m_queued.push_back({.key = key, .modifiers = modifiers, .text = std::string(text)});
        return;
    }
    const uint32_t keysym = Keysym(key, text);
    if (m_ime && keysym != 0 && m_ime->Key(key, keysym, modifiers))
    {
        Changed();
        return;
    }
    Edit(keysym, text);
}

auto Chatbox::Edit(uint32_t keysym, std::string_view text) -> void
{
    switch (keysym)
    {
    case kReturn:
    case kKpEnter: Send(std::exchange(m_draft, {})); break;
    case kEscape: Close(); return;
    case kBackSpace: PopCodepoint(m_draft); break;
    default: m_draft += text; break;
    }
    Changed();
}

auto Chatbox::Attach() -> void
{
    m_ime.reset();
    m_queued.clear();
    m_connecting.reset();
    if (m_script != ImeScript::None)
    {
        m_connecting = std::chrono::steady_clock::now() + kConnectTimeout;
        Connect();
    }
}

auto Chatbox::Connect() -> void
{
    auto connected = ImeContext::Connect(m_script);
    if (const auto now = std::chrono::steady_clock::now(); connected && !*connected && now < m_connecting.value_or(now))
    {
        return;
    }
    m_connecting.reset();
    if (!connected || !*connected)
    {
        const auto reason = connected ? std::string("The input method did not start in time.") : connected.error().Message();
        LOG_WARN("Chatbox input method: {}", reason);
        Toast::Show({.title = "No input method", .body = reason, .timeout = 6.0f});
        m_queued.clear();
        Changed();
        return;
    }
    m_ime = std::move(*connected);
    m_ime->SetOnCommit([this](const std::string& committed) -> void { m_draft += committed; });
    m_ime->SetOnForward([this](uint32_t keysym) -> void { Edit(keysym, KeysymText(keysym)); });
    m_ime->SetOnChange([this]() -> void { Changed(); });
    for (const auto& queued : std::exchange(m_queued, {}))
    {
        Key(queued.key, queued.modifiers, queued.text);
    }
    Changed();
}

auto Chatbox::Open() -> void
{
    if (g_osc == nullptr)
    {
        LOG_WARN("OSC is turned off, the keyboard cannot type into the VRChat chatbox");
        Toast::Show({.title = "Chatbox unavailable", .body = "OSC is turned off in FrameXR.", .timeout = 5.0f});
        return;
    }
    m_open = true;
    m_warned = false;
    m_draft.clear();
    Deliver("/chatbox/typing", {false});
    Attach();
    g_interaction->SetFocusedTextInput(shared_from_this());
    Changed();
}

auto Chatbox::Close() -> void
{
    m_open = false;
    m_draft.clear();
    m_ime.reset();
    m_connecting.reset();
    m_queued.clear();
    if (g_interaction->FocusedTextInput().get() == this)
    {
        g_interaction->SetFocusedTextInput(nullptr);
    }
    Changed();
}

auto Chatbox::Send(std::string text) -> void
{
    Truncate(text, kMaxLength);
    if (!text.empty())
    {
        Deliver("/chatbox/input", {std::move(text), true, true});
    }
}

auto Chatbox::Deliver(const std::string& address, const std::vector<OscValue>& values) -> void
{
    if (g_osc == nullptr)
    {
        return;
    }
    if (const auto sent = g_osc->SendMessage(address, values); !sent)
    {
        LOG_WARN("Could not send {}: {}", address, sent.error().Message());
        if (!std::exchange(m_warned, true))
        {
            Toast::Show({
                .title = "VRChat isn't receiving OSC",
                .body = "Turn it on in VRChat: Action Menu, Options, OSC, Enabled.",
                .timeout = 6.0f,
            });
        }
    }
}

auto Chatbox::Changed() -> void
{
    if (const bool typing = Active() && (!m_draft.empty() || (m_ime && !m_ime->Preedit().empty())); typing != m_typing)
    {
        m_typing = typing;
        Deliver("/chatbox/typing", {typing});
    }
}
