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
    explicit FrameChannel(size_t capacity) : capacity_(capacity) {}

    auto Push(T value) -> std::optional<T> {
        std::scoped_lock lock(mutex_);
        std::optional<T> evicted;
        if (queue_.size() >= capacity_) {
            evicted.emplace(std::move(queue_.front()));
            queue_.pop_front();
        }
        queue_.push_back(std::move(value));
        return evicted;
    }

    auto TakeAll() -> std::deque<T> {
        std::scoped_lock lock(mutex_);
        return std::exchange(queue_, {});
    }

  private:
    size_t capacity_;
    std::mutex mutex_;
    std::deque<T> queue_;
};
