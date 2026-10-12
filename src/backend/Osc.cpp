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

#include "Osc.hpp"

#include <bit>
#include <cmath>
#include <format>
#include <utility>

#include "Keyboard.hpp"
#include "OverlayContainer.hpp"
#include "Watch.hpp"

OscSender* g_osc = nullptr;

namespace {

auto AppendPadded(std::vector<std::byte>& packet, std::string_view text) -> void {
    const auto bytes = std::as_bytes(std::span(text));
    packet.insert(packet.end(), bytes.begin(), bytes.end());
    packet.resize(packet.size() + 4 - (text.size() % 4), std::byte{0});
}

auto AppendBigEndian(std::vector<std::byte>& packet, uint32_t value) -> void {
    for (int shift = 24; shift >= 0; shift -= 8) {
        packet.push_back(static_cast<std::byte>((value >> static_cast<uint32_t>(shift)) & 0xffU));
    }
}

auto Encode(const std::string& address, const std::vector<OscValue>& values) -> std::vector<std::byte> {
    std::string tags = ",";
    std::vector<std::byte> arguments;
    for (const auto& value : values) {
        if (const auto* integer = std::get_if<int32_t>(&value)) {
            tags += 'i';
            AppendBigEndian(arguments, static_cast<uint32_t>(*integer));
        } else if (const auto* real = std::get_if<float>(&value)) {
            tags += 'f';
            AppendBigEndian(arguments, std::bit_cast<uint32_t>(*real));
        } else if (const auto* text = std::get_if<std::string>(&value)) {
            tags += 's';
            AppendPadded(arguments, *text);
        } else {
            tags += std::get<bool>(value) ? 'T' : 'F';
        }
    }

    std::vector<std::byte> packet;
    AppendPadded(packet, address);
    AppendPadded(packet, tags);
    packet.insert(packet.end(), arguments.begin(), arguments.end());
    return packet;
}

auto ElapsedMs(std::chrono::steady_clock::time_point since) -> int64_t {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - since).count();
}

} // namespace

OscSender::OscSender(UdpSocket socket)
    : m_socket(std::move(socket)),
      m_last_sent_overlay(std::chrono::steady_clock::now()),
      m_last_sent_battery(std::chrono::steady_clock::now()) {
}

OscSender::~OscSender() = default;

auto OscSender::Create(uint16_t port) -> Result<std::unique_ptr<OscSender>> {
    auto socket = UdpSocket::Bind(UdpAddress::Any, 0);
    if (!socket) {
        return Unexpected("Failed to bind UDP socket - OSC will not function.");
    }
    if (!socket->ConnectLoopback(port)) {
        return Unexpected("Failed to connect UDP socket - OSC will not function.");
    }
    return std::unique_ptr<OscSender>(new OscSender(std::move(*socket)));
}

auto OscSender::SendMessage(const std::string& address, const std::vector<OscValue>& values) const -> Result<> {
    const auto packet = Encode(address, values);
    if (!m_socket.Send(packet)) {
        return Unexpected("Could not send OSC packet.");
    }
    return {};
}

auto OscSender::SendParams(const std::vector<TrackedDevice>& devices) -> Result<> {
    if (ElapsedMs(m_last_sent_overlay) >= 100) {
        m_last_sent_overlay = std::chrono::steady_clock::now();

        int32_t num_overlays = 0;
        bool has_keyboard = false;
        bool has_wrist = false;
        for (const auto& overlay : g_overlays->Overlays()) {
            const auto& state = overlay->State();
            if (!state.WantVisible()) {
                continue;
            }
            if (state.Name() == kWatchName) {
                has_wrist = true;
            } else if (state.Name() == kKeyboardName) {
                has_keyboard = true;
            } else if (state.Interactable()) {
                ++num_overlays;
            }
        }

        RETURN_IF_ERROR(SendMessage("/avatar/parameters/isOverlayOpen", {num_overlays > 0}));
        RETURN_IF_ERROR(SendMessage("/avatar/parameters/isKeyboardOpen", {has_keyboard}));
        RETURN_IF_ERROR(SendMessage("/avatar/parameters/isWristVisible", {has_wrist}));
        RETURN_IF_ERROR(SendMessage("/avatar/parameters/openOverlayCount", {num_overlays}));
    }

    if (ElapsedMs(m_last_sent_battery) >= 10000) {
        m_last_sent_battery = std::chrono::steady_clock::now();

        int32_t tracker_count = 0;
        int32_t controller_count = 0;
        float tracker_total = 0.0f;
        float controller_total = 0.0f;

        for (const auto& device : devices) {
            const float level = device.soc.value_or(-1.0f);
            std::string parameter;
            switch (device.role) {
            case TrackedDeviceRole::None: continue;
            case TrackedDeviceRole::Hmd:
                RETURN_IF_ERROR(SendMessage("/avatar/parameters/hmdBattery", {static_cast<int32_t>(std::round(level * 100.0f))}));
                parameter = "headset";
                break;
            case TrackedDeviceRole::LeftHand:
                ++controller_count;
                controller_total += level;
                parameter = "leftController";
                break;
            case TrackedDeviceRole::RightHand:
                ++controller_count;
                controller_total += level;
                parameter = "rightController";
                break;
            case TrackedDeviceRole::Tracker:
                ++tracker_count;
                tracker_total += level;
                parameter = std::format("tracker{}", tracker_count);
                break;
            }

            RETURN_IF_ERROR(SendMessage(std::format("/avatar/parameters/{}Battery", parameter), {level}));
            RETURN_IF_ERROR(SendMessage(std::format("/avatar/parameters/{}Charging", parameter), {device.charging}));
        }

        RETURN_IF_ERROR(
            SendMessage("/avatar/parameters/averageControllerBattery", {controller_total / static_cast<float>(controller_count)})
        );
        RETURN_IF_ERROR(
            SendMessage("/avatar/parameters/averageTrackerBattery", {tracker_total / static_cast<float>(tracker_count)})
        );
    }

    return {};
}
