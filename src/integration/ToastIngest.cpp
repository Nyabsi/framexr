// FrameXR
//
// This is a port of wlx-overlay-s from Rust to C++.
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
// Copyright (C) 2023-2025 galister and wlx-overlay-s contributors
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

#include "ToastIngest.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <format>
#include <utility>

#include <nlohmann/json.hpp>

#include "ByteReader.hpp"
#include "ByteWriter.hpp"
#include "Logger.hpp"
#include "Toast.hpp"

namespace
{

constexpr size_t kChannelCapacity = 10;

constexpr std::array<std::byte, 4> kToastMagic{std::byte{'F'}, std::byte{'X'}, std::byte{'R'}, std::byte{'T'}};

constexpr std::byte kToastVersion{1};

constexpr std::byte kToastSoundFlag{0x01};

} // namespace

auto ToastIngest::Utf8Error(std::string_view text) -> std::optional<std::string>
{
    const auto byte = [&](size_t index) -> uint32_t { return static_cast<unsigned char>(text[index]); };
    size_t index = 0;
    while (index < text.size())
    {
        const uint32_t first = byte(index);
        size_t width = 0;
        uint32_t low = 0x80;
        uint32_t high = 0xbf;
        if (first < 0x80)
        {
            ++index;
            continue;
        }
        if (first >= 0xc2 && first <= 0xdf)
        {
            width = 2;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            width = 3;
            low = first == 0xe0 ? 0xa0 : 0x80;
            high = first == 0xed ? 0x9f : 0xbf;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            width = 4;
            low = first == 0xf0 ? 0x90 : 0x80;
            high = first == 0xf4 ? 0x8f : 0xbf;
        }
        else
        {
            return std::format("Utf8Error {{ valid_up_to: {}, error_len: Some(1) }}", index);
        }
        for (size_t offset = 1; offset < width; ++offset)
        {
            if (index + offset >= text.size())
            {
                return std::format("Utf8Error {{ valid_up_to: {}, error_len: None }}", index);
            }
            const uint32_t next = byte(index + offset);
            const uint32_t min = offset == 1 ? low : 0x80;
            const uint32_t max = offset == 1 ? high : 0xbf;
            if (next < min || next > max)
            {
                return std::format("Utf8Error {{ valid_up_to: {}, error_len: Some({}) }}", index, offset);
            }
        }
        index += width;
    }
    return std::nullopt;
}

auto ToastIngest::Encode(const ToastMessage& toast) -> std::vector<std::byte>
{
    ByteWriter writer;
    for (const auto byte : kToastMagic)
    {
        writer.Byte(byte);
    }
    writer.Byte(kToastVersion);
    writer.Byte(static_cast<std::byte>(toast.topic));
    writer.Byte(toast.sound ? kToastSoundFlag : std::byte{0});
    writer.Float(toast.opacity);
    writer.Float(toast.timeout);
    writer.String(toast.title);
    writer.String(toast.body);
    return writer.Take();
}

auto ToastIngest::Decode(std::span<const std::byte> message) -> Result<ToastMessage>
{
    ByteReader reader(message);
    for (const auto expected : kToastMagic)
    {
        if (reader.Byte() != expected)
        {
            return Unexpected("Invalid toast message header");
        }
    }
    if (reader.Byte() != kToastVersion)
    {
        return Unexpected("Unsupported toast message version");
    }
    const auto topic = reader.Byte();
    const auto flags = reader.Byte();
    const auto opacity = reader.Float();
    const auto timeout = reader.Float();
    auto title = reader.String();
    auto body = reader.String();
    if (!topic || !flags || !opacity || !timeout || !title || !body || !reader.Empty())
    {
        return Unexpected("Malformed toast message");
    }
    if (static_cast<uint8_t>(*topic) > static_cast<uint8_t>(ToastTopic::IpdChange))
    {
        return Unexpected("Unknown toast topic");
    }
    return ToastMessage{
        .topic = static_cast<ToastTopic>(*topic),
        .title = std::move(*title),
        .body = std::move(*body),
        .opacity = *opacity,
        .timeout = *timeout,
        .sound = (*flags & kToastSoundFlag) != std::byte{0},
    };
}

auto ToastIngest::FromXsOverlay(std::string_view json) -> Result<std::optional<ToastMessage>>
{
    const auto message = nlohmann::json::parse(json, nullptr, false);
    if (message.is_discarded() || !message.is_object())
    {
        return Unexpected("invalid JSON");
    }
    const auto optional_float = [&](const char* key, float fallback) -> float
    {
        const auto it = message.find(key);
        return it != message.end() && it->is_number() ? it->get<float>() : fallback;
    };

    const auto type = message.find("messageType");
    const auto title = message.find("title");
    if (type == message.end() || !type->is_number_integer())
    {
        return Unexpected("missing field `messageType`");
    }
    if (title == message.end() || !title->is_string())
    {
        return Unexpected("missing field `title`");
    }
    if (type->get<int32_t>() != 1)
    {
        return std::nullopt;
    }

    std::string body;
    if (const auto content = message.find("content"); content != message.end() && content->is_string())
    {
        body = content->get<std::string>();
    }
    return ToastMessage{
        .topic = ToastTopic::XsNotification,
        .title = title->get<std::string>(),
        .body = std::move(body),
        .timeout = optional_float("timeout", 5.0f),
        .sound = optional_float("volume", -1.0f) >= 0.0f,
    };
}

auto ToastIngest::Push(const ToastMessage& toast) -> void
{
    std::scoped_lock lock(m_mutex);
    if (m_messages.size() >= kChannelCapacity)
    {
        LOG_ERROR("Failed to send notification: queue is full");
        return;
    }
    m_messages.push_back(Encode(toast));
}

auto ToastIngest::Drain() -> std::vector<ToastMessage>
{
    std::vector<std::vector<std::byte>> messages;
    {
        std::scoped_lock lock(m_mutex);
        messages.swap(m_messages);
    }
    std::vector<ToastMessage> toasts;
    toasts.reserve(messages.size());
    for (const auto& message : messages)
    {
        if (auto toast = Decode(message))
        {
            toasts.push_back(std::move(*toast));
        }
    }
    return toasts;
}
