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

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "Input.hpp"

class OverlayBackend;
class OverlayState;

using OverlaySelector = std::variant<OverlayId, std::string>;

enum class ColorChannel : uint8_t {
    R,
    G,
    B,
    All,
};

struct ColorGainTask {
    ColorChannel channel = ColorChannel::All;
    float delta = 0.0f;
};

struct ResetPlayspaceTask {};
struct FixFloorTask {};
struct ShowHideTask {};
struct RecenterTask {};
struct OpenEditorTask {};

using SystemTask = std::variant<ColorGainTask, ResetPlayspaceTask, FixFloorTask, ShowHideTask, RecenterTask, OpenEditorTask>;

struct ModifyOverlayTask {
    OverlaySelector selector;
    std::function<void(OverlayState&)> modify;
};

struct CreateOverlayTask {
    OverlaySelector selector;
    std::function<std::optional<std::pair<OverlayState, std::unique_ptr<OverlayBackend>>>()> create;
};

struct DropOverlayTask {
    OverlaySelector selector;
};

using Task = std::variant<ModifyOverlayTask, CreateOverlayTask, DropOverlayTask, SystemTask>;

struct ScheduledTask {
    std::chrono::steady_clock::time_point not_before;
    uint64_t sequence = 0;
    std::shared_ptr<Task> task;
};

class TaskQueue {
  public:
    auto Enqueue(Task task) -> void;
    auto EnqueueAt(Task task, std::chrono::steady_clock::time_point not_before) -> void;
    auto TakeDue(std::deque<Task>& destination) -> void;

  private:
    static auto Later(const ScheduledTask& a, const ScheduledTask& b) -> bool;

    std::priority_queue<ScheduledTask, std::vector<ScheduledTask>, decltype(&Later)> m_tasks{&Later};
    uint64_t m_next_sequence = 0;
};

extern TaskQueue* g_tasks;
