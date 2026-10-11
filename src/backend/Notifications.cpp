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

#include "Notifications.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <utility>

#include <nlohmann/json.hpp>

#include "Logger.hpp"
#include "State.hpp"
#include "UdpSocket.hpp"

namespace {

constexpr size_t kChannelCapacity = 10;
constexpr uint16_t kXsOverlayPort = 42069;
constexpr std::chrono::milliseconds kReceiveTimeout{200};
constexpr std::array<std::byte, 4> kToastMagic{std::byte{'F'}, std::byte{'X'}, std::byte{'R'}, std::byte{'T'}};
constexpr std::byte kToastVersion{1};
constexpr std::byte kToastSoundFlag{0x01};

} // namespace

ByteReader::ByteReader(std::span<const std::byte> bytes) : bytes_(bytes) {
}

auto ByteWriter::Byte(std::byte value) -> void {
    bytes_.push_back(value);
}

auto ByteWriter::Float(float value) -> void {
    const auto raw = std::bit_cast<uint32_t>(value);
    for (uint32_t shift = 0; shift < 32; shift += 8) {
        bytes_.push_back(static_cast<std::byte>((raw >> shift) & 0xffU));
    }
}

auto ByteWriter::String(std::string_view value) -> void {
    const auto length = static_cast<uint16_t>(std::min<size_t>(value.size(), UINT16_MAX));
    bytes_.push_back(static_cast<std::byte>(length & 0xffU));
    bytes_.push_back(static_cast<std::byte>(length >> 8U));
    const auto truncated = value.substr(0, length);
    const auto data = std::as_bytes(std::span(truncated.begin(), truncated.end()));
    bytes_.insert(bytes_.end(), data.begin(), data.end());
}

auto ByteWriter::Take() -> std::vector<std::byte> {
    return std::move(bytes_);
}

auto ByteReader::Byte() -> std::optional<std::byte> {
    if (bytes_.empty()) {
        return std::nullopt;
    }
    const auto value = bytes_.front();
    bytes_ = bytes_.subspan(1);
    return value;
}

auto ByteReader::Float() -> std::optional<float> {
    if (bytes_.size() < 4) {
        return std::nullopt;
    }
    uint32_t raw = 0;
    for (size_t i = 0; i < 4; ++i) {
        raw |= static_cast<uint32_t>(bytes_[i]) << (i * 8);
    }
    bytes_ = bytes_.subspan(4);
    return std::bit_cast<float>(raw);
}

auto ByteReader::String() -> std::optional<std::string> {
    if (bytes_.size() < 2) {
        return std::nullopt;
    }
    const size_t length = static_cast<size_t>(bytes_[0]) | (static_cast<size_t>(bytes_[1]) << 8U);
    if (bytes_.size() < 2 + length) {
        return std::nullopt;
    }
    std::string value(length, '\0');
    std::memcpy(value.data(), bytes_.subspan(2).data(), length);
    bytes_ = bytes_.subspan(2 + length);
    return value;
}

auto ByteReader::Empty() const -> bool {
    return bytes_.empty();
}

auto ToastIngest::Utf8Error(std::string_view text) -> std::optional<std::string> {
    const auto byte = [&](size_t index) -> uint32_t { return static_cast<unsigned char>(text[index]); };
    size_t index = 0;
    while (index < text.size()) {
        const uint32_t first = byte(index);
        size_t width = 0;
        uint32_t low = 0x80;
        uint32_t high = 0xbf;
        if (first < 0x80) {
            ++index;
            continue;
        }
        if (first >= 0xc2 && first <= 0xdf) {
            width = 2;
        } else if (first >= 0xe0 && first <= 0xef) {
            width = 3;
            low = first == 0xe0 ? 0xa0 : 0x80;
            high = first == 0xed ? 0x9f : 0xbf;
        } else if (first >= 0xf0 && first <= 0xf4) {
            width = 4;
            low = first == 0xf0 ? 0x90 : 0x80;
            high = first == 0xf4 ? 0x8f : 0xbf;
        } else {
            return std::format("Utf8Error {{ valid_up_to: {}, error_len: Some(1) }}", index);
        }
        for (size_t offset = 1; offset < width; ++offset) {
            if (index + offset >= text.size()) {
                return std::format("Utf8Error {{ valid_up_to: {}, error_len: None }}", index);
            }
            const uint32_t next = byte(index + offset);
            const uint32_t min = offset == 1 ? low : 0x80;
            const uint32_t max = offset == 1 ? high : 0xbf;
            if (next < min || next > max) {
                return std::format("Utf8Error {{ valid_up_to: {}, error_len: Some({}) }}", index, offset);
            }
        }
        index += width;
    }
    return std::nullopt;
}

auto ToastIngest::Encode(const Toast& toast) -> std::vector<std::byte> {
    ByteWriter writer;
    for (const auto byte : kToastMagic) {
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

auto ToastIngest::Decode(std::span<const std::byte> message) -> Result<Toast> {
    ByteReader reader(message);
    for (const auto expected : kToastMagic) {
        if (reader.Byte() != expected) {
            return Unexpected("Invalid toast message header");
        }
    }
    if (reader.Byte() != kToastVersion) {
        return Unexpected("Unsupported toast message version");
    }
    const auto topic = reader.Byte();
    const auto flags = reader.Byte();
    const auto opacity = reader.Float();
    const auto timeout = reader.Float();
    auto title = reader.String();
    auto body = reader.String();
    if (!topic || !flags || !opacity || !timeout || !title || !body || !reader.Empty()) {
        return Unexpected("Malformed toast message");
    }
    if (static_cast<uint8_t>(*topic) > static_cast<uint8_t>(ToastTopic::IpdChange)) {
        return Unexpected("Unknown toast topic");
    }
    return Toast{
        .topic = static_cast<ToastTopic>(*topic),
        .title = std::move(*title),
        .body = std::move(*body),
        .opacity = *opacity,
        .timeout = *timeout,
        .sound = (*flags & kToastSoundFlag) != std::byte{0},
    };
}

auto ToastIngest::FromXsOverlay(std::string_view json) -> Result<std::optional<Toast>> {
    const auto message = nlohmann::json::parse(json, nullptr, false);
    if (message.is_discarded() || !message.is_object()) {
        return Unexpected("invalid JSON");
    }
    const auto optional_float = [&](const char* key, float fallback) -> float {
        const auto it = message.find(key);
        return it != message.end() && it->is_number() ? it->get<float>() : fallback;
    };

    const auto type = message.find("messageType");
    const auto title = message.find("title");
    if (type == message.end() || !type->is_number_integer()) {
        return Unexpected("missing field `messageType`");
    }
    if (title == message.end() || !title->is_string()) {
        return Unexpected("missing field `title`");
    }
    if (type->get<int32_t>() != 1) {
        return std::nullopt;
    }

    std::string body;
    if (const auto content = message.find("content"); content != message.end() && content->is_string()) {
        body = content->get<std::string>();
    }
    return Toast{
        .topic = ToastTopic::XsNotification,
        .title = title->get<std::string>(),
        .body = std::move(body),
        .timeout = optional_float("timeout", 5.0f),
        .sound = optional_float("volume", -1.0f) >= 0.0f,
    };
}

auto ToastIngest::Push(const Toast& toast) -> void {
    std::scoped_lock lock(mutex_);
    if (messages_.size() >= kChannelCapacity) {
        LOG_ERROR("Failed to send notification: queue is full");
        return;
    }
    messages_.push_back(Encode(toast));
}

auto ToastIngest::Drain() -> std::vector<Toast> {
    std::vector<std::vector<std::byte>> messages;
    {
        std::scoped_lock lock(mutex_);
        messages.swap(messages_);
    }
    std::vector<Toast> toasts;
    toasts.reserve(messages.size());
    for (const auto& message : messages) {
        if (auto toast = Decode(message)) {
            toasts.push_back(std::move(*toast));
        }
    }
    return toasts;
}

NotificationManager::NotificationManager()
    : ingest_(std::make_shared<ToastIngest>()),
      running_(std::make_shared<std::atomic<bool>>(true)) {
}

NotificationManager::~NotificationManager() {
    running_->store(false, std::memory_order_relaxed);
    if (udp_thread_.joinable()) {
        udp_thread_.join();
    }
    if (native_thread_.joinable()) {
        native_thread_.join();
    }
    CloseDesktop();
}

auto NotificationManager::SubmitPending(AppState& app) -> void {
    PollDesktop();

    auto toasts = ingest_->Drain();
    if (!app.config.notifications_enabled) {
        return;
    }
    for (const auto& toast : toasts) {
        toast.Submit(app);
    }
}

auto NotificationManager::RunUdp() -> void {
    udp_thread_ = std::thread([ingest = ingest_, running = running_] -> void {
        auto socket = UdpSocket::Bind(UdpAddress::Loopback, kXsOverlayPort);
        if (!socket) {
            LOG_ERROR("Failed to bind notification socket @ 127.0.0.1:{}: {}", kXsOverlayPort, socket.error().Message());
            return;
        }
        if (const auto result = socket->SetReceiveTimeout(kReceiveTimeout); !result) {
            LOG_ERROR("Failed to set read timeout: {}", result.error().Message());
        }

        std::vector<std::byte> buffer(size_t{16} * 1024);
        while (running->load(std::memory_order_relaxed)) {
            const auto received = socket->Receive(buffer);
            if (!received) {
                continue;
            }
            const std::string_view payload(reinterpret_cast<const char*>(buffer.data()), *received);
            if (const auto utf8_error = ToastIngest::Utf8Error(payload)) {
                LOG_ERROR("Failed to receive notification message: {}", *utf8_error);
                continue;
            }
            auto toast = ToastIngest::FromXsOverlay(payload);
            if (!toast) {
                LOG_ERROR("Failed to parse notification message: {}", toast.error().Message());
                continue;
            }
            if (*toast) {
                ingest->Push(**toast);
            }
        }
        LOG_INFO("Notification listener stopped.");
    });

    RunNative();
}
