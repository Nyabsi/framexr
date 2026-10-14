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
#include <memory>
#include <vector>

#include "HidProvider.hpp"

struct HidEvent
{
    VirtualKey key = VirtualKey::Escape;
    bool down = false;
    uint8_t modifiers = 0;

    auto operator==(const HidEvent&) const -> bool = default;
};

struct ButtonEvent
{
    uint16_t button = 0;
    bool down = false;

    auto operator==(const ButtonEvent&) const -> bool = default;
};

class RecordingHid final : public HidProvider
{
  public:
    [[nodiscard]] auto Events() const -> const std::vector<HidEvent>& { return m_events; }
    [[nodiscard]] auto Buttons() const -> const std::vector<ButtonEvent>& { return m_buttons; }
    [[nodiscard]] auto Wheels() const -> size_t { return m_wheels; }
    [[nodiscard]] auto InputMethods() const -> const std::vector<ImeScript>& { return m_input_methods; }

    auto MouseMove(const Eigen::Vector2f& position) -> void override;
    auto SendButton(uint16_t button, bool down) -> void override;
    auto Wheel(int32_t delta_y, int32_t delta_x) -> void override;
    auto SetModifiers(uint8_t value) -> void override;
    auto SendKey(VirtualKey key, bool down) -> void override;
    auto SetDesktopExtent(const Eigen::Vector2f& extent) -> void override;
    auto SetDesktopOrigin(const Eigen::Vector2f& origin) -> void override;
    auto Commit() -> void override;
    auto SelectInputMethod(ImeScript script) -> void override;
    auto Clear() -> void;

  private:
    std::vector<HidEvent> m_events;
    std::vector<ButtonEvent> m_buttons;
    size_t m_wheels = 0;
    std::vector<ImeScript> m_input_methods;
    uint8_t m_modifiers = 0;
};
