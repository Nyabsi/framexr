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

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

template <typename Document>
class History {
  public:
    static constexpr size_t kLimit = 100;

    explicit History(Document document) : current_(std::move(document)) {}

    [[nodiscard]] auto Current() const -> const Document& { return current_; }
    [[nodiscard]] auto Live() -> Document& { return current_; }
    [[nodiscard]] auto CanUndo() const -> bool { return !undo_.empty(); }
    [[nodiscard]] auto CanRedo() const -> bool { return !redo_.empty(); }
    [[nodiscard]] auto Dirty() const -> bool { return version_ != saved_version_; }
    [[nodiscard]] auto Version() const -> uint64_t { return version_; }

    template <typename Change>
    auto Edit(Change&& change) -> void {
        auto before = current_;
        std::forward<Change>(change)(current_);
        Commit(std::move(before));
    }

    template <typename Change>
    auto Amend(Change&& change) -> void {
        std::forward<Change>(change)(current_);
        version_ = ++next_version_;
    }

    auto Commit(Document before) -> void {
        undo_.push_back({std::move(before), version_});
        if (undo_.size() > kLimit) {
            undo_.erase(undo_.begin());
        }
        redo_.clear();
        version_ = ++next_version_;
    }

    auto Undo() -> bool { return Step(undo_, redo_); }
    auto Redo() -> bool { return Step(redo_, undo_); }
    auto MarkSaved() -> void { saved_version_ = version_; }

    auto Reset(Document document) -> void {
        current_ = std::move(document);
        undo_.clear();
        redo_.clear();
        version_ = ++next_version_;
        saved_version_ = version_;
    }

  private:
    struct Entry {
        Document document;
        uint64_t version = 0;
    };

    auto Step(std::vector<Entry>& from, std::vector<Entry>& to) -> bool {
        if (from.empty()) {
            return false;
        }
        to.push_back({std::move(current_), version_});
        current_ = std::move(from.back().document);
        version_ = from.back().version;
        from.pop_back();
        return true;
    }

    Document current_;
    std::vector<Entry> undo_;
    std::vector<Entry> redo_;
    uint64_t version_ = 0;
    uint64_t saved_version_ = 0;
    uint64_t next_version_ = 0;
};
