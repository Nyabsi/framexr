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

#include "Task.hpp"

auto ScheduledTask::operator<(const ScheduledTask& other) const -> bool {
    if (not_before != other.not_before) {
        return not_before > other.not_before;
    }
    return sequence > other.sequence;
}

auto TaskQueue::Enqueue(Task task) -> void {
    EnqueueAt(std::move(task), std::chrono::steady_clock::now());
}

auto TaskQueue::EnqueueAt(Task task, std::chrono::steady_clock::time_point not_before) -> void {
    tasks_.push({.not_before = not_before, .sequence = next_sequence_++, .task = std::make_shared<Task>(std::move(task))});
}

auto TaskQueue::TakeDue(std::deque<Task>& destination) -> void {
    const auto now = std::chrono::steady_clock::now();
    while (!tasks_.empty() && tasks_.top().not_before <= now) {
        destination.push_back(std::move(*tasks_.top().task));
        tasks_.pop();
    }
}
