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

#include <chrono>
#include <cstdint>
#include <filesystem>

class Editor;

enum class EditTarget : uint8_t;

class Drafts final {
  public:
    static auto File(EditTarget target) -> std::filesystem::path;

    auto Save(const Editor& editor, bool force) -> void;
    auto Restore(Editor& editor) -> bool;

  private:
    uint64_t m_watch = 0;
    uint64_t m_keyboard = 0;
    uint64_t m_theme = 0;
    std::chrono::steady_clock::time_point m_last = std::chrono::steady_clock::now();
};
