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
#include <vector>

#include <Eigen/Core>

#include "ImeScript.hpp"
#include "VirtualKeys.hpp"

constexpr uint16_t kMouseLeft = 0x110;

constexpr uint16_t kMouseRight = 0x111;

constexpr uint16_t kMouseMiddle = 0x112;

class HidProvider {
  public:
    static auto Create() -> std::unique_ptr<HidProvider>;
    static auto UseNative() -> bool { return m_use_native.load(); }
    static auto SetUseNative(bool use_native) -> void { m_use_native.store(use_native); }

    virtual ~HidProvider() = default;

    virtual auto MouseMove(const Eigen::Vector2f& position) -> void = 0;
    virtual auto SendButton(uint16_t button, bool down) -> void = 0;
    virtual auto Wheel(int32_t delta_y, int32_t delta_x) -> void = 0;
    virtual auto SetModifiers(uint8_t modifiers) -> void = 0;
    virtual auto SendKey(VirtualKey key, bool down) -> void = 0;
    virtual auto SetDesktopExtent(const Eigen::Vector2f& extent) -> void = 0;
    virtual auto SetDesktopOrigin(const Eigen::Vector2f& origin) -> void = 0;
    virtual auto Commit() -> void = 0;
    virtual auto SelectInputMethod(ImeScript script) -> void = 0;

  private:
    static inline std::atomic<bool> m_use_native{true};
};

struct MouseButtonAction {
    uint16_t button = 0;
    bool down = false;
};

struct PendingMouseAction {
    std::optional<Eigen::Vector2f> last_requested_position;
    std::optional<Eigen::Vector2f> position;
    std::vector<MouseButtonAction> buttons;
    std::optional<Eigen::Vector2i> scroll;
};

extern HidProvider* g_hid;
