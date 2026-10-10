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

#pragma once

#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string_view>

#include <Eigen/Core>

#include "VirtualKey.hpp"

constexpr uint16_t kMouseLeft = 0x110;
constexpr uint16_t kMouseRight = 0x111;
constexpr uint16_t kMouseMiddle = 0x112;

class HidProvider {
  public:
    static auto Create() -> std::unique_ptr<HidProvider>;
    static inline std::atomic<bool> use_uinput{true};

    virtual ~HidProvider() = default;

    virtual auto MouseMove(const Eigen::Vector2f& position) -> void = 0;
    virtual auto SendButton(uint16_t button, bool down) -> void = 0;
    virtual auto Wheel(int32_t delta_y, int32_t delta_x) -> void = 0;
    virtual auto SetModifiers(uint8_t modifiers) -> void = 0;
    virtual auto SendKey(VirtualKey key, bool down) -> void = 0;
    virtual auto SetDesktopExtent(const Eigen::Vector2f& extent) -> void = 0;
    virtual auto SetDesktopOrigin(const Eigen::Vector2f& origin) -> void = 0;
    virtual auto Commit() -> void = 0;
};

class NullHidProvider final : public HidProvider {
  public:
    auto MouseMove(const Eigen::Vector2f& position) -> void override;
    auto SendButton(uint16_t button, bool down) -> void override;
    auto Wheel(int32_t delta_y, int32_t delta_x) -> void override;
    auto SetModifiers(uint8_t modifiers) -> void override;
    auto SendKey(VirtualKey key, bool down) -> void override;
    auto SetDesktopExtent(const Eigen::Vector2f& extent) -> void override;
    auto SetDesktopOrigin(const Eigen::Vector2f& origin) -> void override;
    auto Commit() -> void override;
};

struct MouseButtonAction {
    uint16_t button = 0;
    bool down = false;
};

struct PendingMouseAction {
    std::optional<Eigen::Vector2f> last_requested_position;
    std::optional<Eigen::Vector2f> position;
    std::optional<MouseButtonAction> button;
    std::optional<Eigen::Vector2i> scroll;
};

struct UInputEvent {
    uint16_t type = 0;
    uint16_t code = 0;
    int32_t value = 0;
};

class UInputProvider final : public HidProvider {
  public:
    static auto Create() -> std::unique_ptr<UInputProvider>;

    ~UInputProvider() override;
    UInputProvider(const UInputProvider&) = delete;
    auto operator=(const UInputProvider&) -> UInputProvider& = delete;

    auto MouseMove(const Eigen::Vector2f& position) -> void override;
    auto SendButton(uint16_t button, bool down) -> void override;
    auto Wheel(int32_t delta_y, int32_t delta_x) -> void override;
    auto SetModifiers(uint8_t modifiers) -> void override;
    auto SendKey(VirtualKey key, bool down) -> void override;
    auto SetDesktopExtent(const Eigen::Vector2f& extent) -> void override;
    auto SetDesktopOrigin(const Eigen::Vector2f& origin) -> void override;
    auto Commit() -> void override;

  private:
    UInputProvider(int keyboard_fd, int mouse_fd);

    auto Write(int fd, std::initializer_list<UInputEvent> events, std::string_view context) const -> void;

    int keyboard_fd_;
    int mouse_fd_;
    Eigen::Vector2f desktop_extent_ = Eigen::Vector2f::Zero();
    Eigen::Vector2f desktop_origin_ = Eigen::Vector2f::Zero();
    uint8_t current_modifiers_ = 0;
    PendingMouseAction pending_;
};
