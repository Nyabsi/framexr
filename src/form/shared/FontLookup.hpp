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

#include <cstdint>
#include <optional>
#include <string>

struct FT_LibraryRec_;

struct FontFile {
    std::string path;
    std::string name;
    int index = 0;
};

class FontLookup final {
  public:
    explicit FontLookup(std::string primary_font);

    [[nodiscard]] auto Locate(FT_LibraryRec_* library, uint32_t codepoint, int size) const -> std::optional<FontFile>;

  private:
    std::string m_primary_font;
};
