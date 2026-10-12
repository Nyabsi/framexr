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

#include "Input.hpp"

#include <algorithm>

#include "Config.hpp"
#include "Logger.hpp"

InputState* g_input = nullptr;

namespace {

constexpr float kMaxDeltaSeconds = 0.1f;

} // namespace

InputState::InputState() : m_pointers{Pointer(0), Pointer(1)} {
}

auto InputState::PreUpdate() -> void {
    const auto now = std::chrono::steady_clock::now();
    m_delta_seconds = std::min(std::chrono::duration<float>(now - m_time).count(), kMaxDeltaSeconds);
    m_time = now;
    for (auto& pointer : m_pointers) {
        pointer.SetBefore(pointer.Now());
    }
}

auto InputState::LogChanges(const Pointer& pointer) const -> void {
    const auto log = [&](std::string_view name, bool now, bool before) -> void {
        if (now != before) {
            LOG_DEBUG("Hand {}: {} {}", pointer.Index(), name, now);
        }
    };
    log("click", pointer.Now().click, pointer.Before().click);
    log("grab", pointer.Now().grab, pointer.Before().grab);
    log("alt_click", pointer.Now().alt_click, pointer.Before().alt_click);
    log("space_drag", pointer.Now().space_drag, pointer.Before().space_drag);
    log("space_rotate", pointer.Now().space_rotate, pointer.Before().space_rotate);
    log("space_reset", pointer.Now().space_reset, pointer.Before().space_reset);
    log("click_modifier_right", pointer.Now().click_modifier_right, pointer.Before().click_modifier_right);
    log("click_modifier_middle", pointer.Now().click_modifier_middle, pointer.Before().click_modifier_middle);
}

auto InputState::PostUpdate() -> void {
    for (auto& pointer : m_pointers) {
        pointer.SetTrackedPose(pointer.Pose());
        if (g_config->pointer_smoothing) {
            pointer.SetPose(pointer.AimFilter().Filter(pointer.Pose(), m_delta_seconds));
        } else {
            pointer.AimFilter().Reset();
        }
        LogChanges(pointer);

        if (pointer.Now().click) {
            pointer.SetLastClick(std::chrono::steady_clock::now());
        }
        if (pointer.Now().click_modifier_right) {
            pointer.Interaction().mode = PointerMode::Right;
            continue;
        }
        if (pointer.Now().click_modifier_middle) {
            pointer.Interaction().mode = PointerMode::Middle;
            continue;
        }
        pointer.Interaction().mode = PointerMode::Left;

        if (pointer.Now().alt_click == pointer.Before().alt_click) {
            continue;
        }
        std::erase_if(m_processes, [](Process& process) -> bool {
            const auto status = process.TryWait();
            return status && status->has_value();
        });
        const auto& command = pointer.Now().alt_click ? g_config->alt_click_down : g_config->alt_click_up;
        if (!command.empty()) {
            if (auto process = Process::Spawn(command)) {
                m_processes.push_back(std::move(*process));
            }
        }
    }
}

auto InputState::SortDevices() -> void {
    std::ranges::sort(m_devices, [](const TrackedDevice& a, const TrackedDevice& b) -> bool {
        const auto key = [](const TrackedDevice& device) -> std::tuple<int, uint8_t, float> {
            return std::tuple(device.soc.has_value() ? 0 : 1, static_cast<uint8_t>(device.role), device.soc.value_or(999.0f));
        };
        return key(a) < key(b);
    });
}

auto InputState::Interact() -> std::array<InteractionResult, 2> {
    auto& [left, right] = g_input->Pointers();
    if (right.LastClick() > left.LastClick()) {
        auto right_result = right.Interact();
        auto left_result = left.Interact();
        return {left_result, right_result};
    }
    auto left_result = left.Interact();
    auto right_result = right.Interact();
    return {left_result, right_result};
}
