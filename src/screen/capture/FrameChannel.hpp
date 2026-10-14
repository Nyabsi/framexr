// FrameXR
//
// This is a port of wlx-capture from Rust to C++.
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
// Copyright (C) 2023-2025 galister and wlx-capture contributors
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

#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

template <typename T>
class FrameChannel final {
  public:
    explicit FrameChannel(size_t capacity) : m_capacity(capacity) {}

    auto Push(T value) -> std::optional<T> {
        std::scoped_lock lock(m_mutex);
        std::optional<T> evicted;
        if (m_queue.size() >= m_capacity) {
            evicted.emplace(std::move(m_queue.front()));
            m_queue.pop_front();
        }
        m_queue.push_back(std::move(value));
        return evicted;
    }

    auto TakeAll() -> std::deque<T> {
        std::scoped_lock lock(m_mutex);
        return std::exchange(m_queue, {});
    }

  private:
    size_t m_capacity;
    std::mutex m_mutex;
    std::deque<T> m_queue;
};
