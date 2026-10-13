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

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <Eigen/Geometry>

#include "OneEuroFilter.hpp"
#include "methods/LaserPointer.hpp"
#include "Process.hpp"

class Keymap;

class TextInput;

class Interaction {
  public:
    Interaction();

    [[nodiscard]] auto Hmd() const -> const Eigen::Affine3f& { return m_hmd; }
    [[nodiscard]] auto Hmd() -> Eigen::Affine3f& { return m_hmd; }
    [[nodiscard]] auto Ipd() const -> float { return m_ipd; }
    [[nodiscard]] auto DeltaSeconds() const -> float { return m_delta_seconds; }
    [[nodiscard]] auto Pointers() const -> const std::array<LaserPointer, 2>& { return m_pointers; }
    [[nodiscard]] auto Pointers() -> std::array<LaserPointer, 2>& { return m_pointers; }
    [[nodiscard]] auto DisplayPixelsPerRadian() const -> const Eigen::Vector2f& { return m_display_pixels_per_radian; }
    [[nodiscard]] auto DisplayPixelsPerRadian() -> Eigen::Vector2f& { return m_display_pixels_per_radian; }
    [[nodiscard]] auto TypingKeymap() const -> const std::shared_ptr<const Keymap>& { return m_typing_keymap; }
    [[nodiscard]] auto FocusedTextInput() const -> std::shared_ptr<TextInput> { return m_focused_text_input; }

    auto SetHmd(Eigen::Affine3f hmd) -> void { m_hmd = std::move(hmd); }
    auto SetIpd(float ipd) -> void { m_ipd = ipd; }
    auto SetDeltaSeconds(float delta_seconds) -> void { m_delta_seconds = delta_seconds; }
    auto SetDisplayPixelsPerRadian(Eigen::Vector2f display_pixels_per_radian) -> void {
        m_display_pixels_per_radian = std::move(display_pixels_per_radian);
    }
    auto SetTypingKeymap(std::shared_ptr<const Keymap> keymap) -> void { m_typing_keymap = std::move(keymap); }
    auto SetFocusedTextInput(std::shared_ptr<TextInput> input) -> void { m_focused_text_input = std::move(input); }

    auto PreUpdate() -> void;
    auto PostUpdate() -> void;

    static auto Interact() -> std::array<InteractionResult, 2>;

  private:
    auto LogChanges(const LaserPointer& pointer) const -> void;

    Eigen::Affine3f m_hmd = Eigen::Affine3f::Identity();
    float m_ipd = 0.0f;
    std::chrono::steady_clock::time_point m_time = std::chrono::steady_clock::now();
    float m_delta_seconds = 0.0f;
    std::array<LaserPointer, 2> m_pointers;
    Eigen::Vector2f m_display_pixels_per_radian = Eigen::Vector2f::Zero();
    std::shared_ptr<const Keymap> m_typing_keymap;
    std::shared_ptr<TextInput> m_focused_text_input;
    std::vector<Process> m_processes;
};

extern Interaction* g_interaction;
