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

TaskQueue* g_tasks = nullptr;

auto TaskQueue::Later(const ScheduledTask& a, const ScheduledTask& b) -> bool {
    if (a.not_before != b.not_before) {
        return a.not_before > b.not_before;
    }
    return a.sequence > b.sequence;
}

auto TaskQueue::Enqueue(Task task) -> void {
    EnqueueAt(std::move(task), std::chrono::steady_clock::now());
}

auto TaskQueue::EnqueueAt(Task task, std::chrono::steady_clock::time_point not_before) -> void {
    m_tasks.push({.not_before = not_before, .sequence = m_next_sequence++, .task = std::make_shared<Task>(std::move(task))});
}

auto TaskQueue::TakeDue(std::deque<Task>& destination) -> void {
    const auto now = std::chrono::steady_clock::now();
    while (!m_tasks.empty() && m_tasks.top().not_before <= now) {
        destination.push_back(std::move(*m_tasks.top().task));
        m_tasks.pop();
    }
}
