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
class History
{
  public:
    static constexpr size_t kLimit = 100;

    explicit History(Document document) : m_current(std::move(document)) {}

    [[nodiscard]] auto Current() const -> const Document& { return m_current; }
    [[nodiscard]] auto Live() -> Document& { return m_current; }
    [[nodiscard]] auto CanUndo() const -> bool { return !m_undo.empty(); }
    [[nodiscard]] auto CanRedo() const -> bool { return !m_redo.empty(); }
    [[nodiscard]] auto Dirty() const -> bool { return m_version != m_saved_version; }
    [[nodiscard]] auto Version() const -> uint64_t { return m_version; }

    template <typename Change>
    auto Edit(Change&& change) -> void
    {
        auto before = m_current;
        std::forward<Change>(change)(m_current);
        Commit(std::move(before));
    }

    template <typename Change>
    auto Amend(Change&& change) -> void
    {
        std::forward<Change>(change)(m_current);
        m_version = ++m_next_version;
    }

    auto Commit(Document before) -> void
    {
        m_undo.push_back({std::move(before), m_version});
        if (m_undo.size() > kLimit)
        {
            m_undo.erase(m_undo.begin());
        }
        m_redo.clear();
        m_version = ++m_next_version;
    }

    auto Undo() -> bool { return Step(m_undo, m_redo); }
    auto Redo() -> bool { return Step(m_redo, m_undo); }
    auto MarkSaved() -> void { m_saved_version = m_version; }

    auto Reset(Document document) -> void
    {
        m_current = std::move(document);
        m_undo.clear();
        m_redo.clear();
        m_version = ++m_next_version;
        m_saved_version = m_version;
    }

  private:
    struct Entry
    {
        Document document;
        uint64_t version = 0;
    };

    auto Step(std::vector<Entry>& from, std::vector<Entry>& to) -> bool
    {
        if (from.empty())
        {
            return false;
        }
        to.push_back({std::move(m_current), m_version});
        m_current = std::move(from.back().document);
        m_version = from.back().version;
        from.pop_back();
        return true;
    }

    Document m_current;
    std::vector<Entry> m_undo;
    std::vector<Entry> m_redo;
    uint64_t m_version = 0;
    uint64_t m_saved_version = 0;
    uint64_t m_next_version = 0;
};
