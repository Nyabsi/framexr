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

#include "Hid.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "Logger.hpp"
#include "Scancodes.hpp"

namespace {

constexpr float kAbsoluteExtent = 65535.0f;

auto KeyInput(VirtualKey key, bool down) -> std::optional<INPUT> {
    INPUT input{.type = INPUT_KEYBOARD};
    const DWORD release = down ? 0 : KEYEVENTF_KEYUP;
    if (key == VirtualKey::Pause) {
        input.ki.wVk = VK_PAUSE;
        input.ki.dwFlags = release;
        return input;
    }
    const auto scancode = Scancodes::ForKey(key);
    if (!scancode) {
        return std::nullopt;
    }
    input.ki.wScan = static_cast<WORD>(*scancode & 0xFF);
    input.ki.dwFlags = KEYEVENTF_SCANCODE | release | ((*scancode & kScancodeExtended) != 0 ? KEYEVENTF_EXTENDEDKEY : 0);
    return input;
}

auto ButtonFlag(uint16_t button, bool down) -> DWORD {
    switch (button) {
    case kMouseRight: return down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    case kMouseMiddle: return down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    default: return down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    }
}

auto Send(std::vector<INPUT>& inputs, std::string_view context) -> void {
    if (inputs.empty()) {
        return;
    }
    const UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    if (sent != inputs.size()) {
        LOG_ERROR("{}SendInput delivered {} of {} events (error {})", context, sent, inputs.size(), GetLastError());
    }
}

} // namespace

auto HidProvider::Create() -> std::unique_ptr<HidProvider> {
    if (!use_uinput.load()) {
        LOG_INFO("Input injection disabled by user.");
        return std::make_unique<NullHidProvider>();
    }
    LOG_INFO("Initialized SendInput.");
    return std::make_unique<SendInputProvider>();
}

auto SendInputProvider::SetModifiers(uint8_t modifiers) -> void {
    const uint8_t changed = current_modifiers_ ^ modifiers;
    for (int bit = 0; bit < 8; ++bit) {
        const auto mask = static_cast<uint8_t>(1U << bit);
        if ((changed & mask) == 0) {
            continue;
        }
        if (const auto key = VirtualKeys::KeyForModifier(mask)) {
            SendKey(*key, (modifiers & mask) != 0);
        }
    }
    current_modifiers_ = modifiers;
}

auto SendInputProvider::SendKey(VirtualKey key, bool down) -> void {
    LOG_TRACE("send_key: {} {}", VirtualKeys::Name(key), down);
    auto input = KeyInput(key, down);
    if (!input) {
        LOG_DEBUG("send_key: {} has no Windows scancode", VirtualKeys::Name(key));
        return;
    }
    std::vector<INPUT> inputs{*input};
    Send(inputs, "send_key: ");
}

auto SendInputProvider::SetDesktopExtent(const Eigen::Vector2f& extent) -> void {
    desktop_extent_ = extent.cwiseMax(Eigen::Vector2f::Ones());
}

auto SendInputProvider::SetDesktopOrigin(const Eigen::Vector2f& origin) -> void {
    desktop_origin_ = origin;
}

auto SendInputProvider::MouseMove(const Eigen::Vector2f& position) -> void {
    if (!pending_.position && !pending_.scroll) {
        pending_.position = position;
    }
    pending_.last_requested_position = position;
}

auto SendInputProvider::SendButton(uint16_t button, bool down) -> void {
    if (pending_.buttons.empty()) {
        pending_.position = pending_.last_requested_position;
    }
    pending_.buttons.push_back({.button = button, .down = down});
}

auto SendInputProvider::Wheel(int32_t delta_y, int32_t delta_x) -> void {
    if (!pending_.scroll) {
        pending_.scroll = Eigen::Vector2i(delta_x, delta_y);
        pending_.position.reset();
    }
}

auto SendInputProvider::Commit() -> void {
    std::vector<INPUT> inputs;
    if (const auto position = std::exchange(pending_.position, std::nullopt)) {
        LOG_TRACE("Mouse move: {} {}", position->x(), position->y());
        const Eigen::Vector2f span = (desktop_extent_ - Eigen::Vector2f::Ones()).cwiseMax(Eigen::Vector2f::Ones());
        const Eigen::Vector2f
            scaled = ((*position - desktop_origin_).cwiseQuotient(span) * kAbsoluteExtent).cwiseMax(0.0f).cwiseMin(kAbsoluteExtent);
        INPUT move{.type = INPUT_MOUSE};
        move.mi.dx = std::lround(scaled.x());
        move.mi.dy = std::lround(scaled.y());
        move.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        inputs.push_back(move);
    }
    for (const auto& button : std::exchange(pending_.buttons, {})) {
        INPUT click{.type = INPUT_MOUSE};
        click.mi.dwFlags = ButtonFlag(button.button, button.down);
        inputs.push_back(click);
    }
    if (const auto scroll = std::exchange(pending_.scroll, std::nullopt)) {
        if (scroll->y() != 0) {
            INPUT wheel{.type = INPUT_MOUSE};
            wheel.mi.mouseData = static_cast<DWORD>(scroll->y());
            wheel.mi.dwFlags = MOUSEEVENTF_WHEEL;
            inputs.push_back(wheel);
        }
        if (scroll->x() != 0) {
            INPUT wheel{.type = INPUT_MOUSE};
            wheel.mi.mouseData = static_cast<DWORD>(scroll->x());
            wheel.mi.dwFlags = MOUSEEVENTF_HWHEEL;
            inputs.push_back(wheel);
        }
    }
    Send(inputs, "");
}
