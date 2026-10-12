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

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "KeyboardLayout.hpp"

class KeyboardProfiles final {
  public:
    static auto UserDirectory() -> std::filesystem::path;
    static auto IsBuiltIn(std::string_view name) -> bool;
    static auto Load(std::span<const std::string_view> built_in, const std::filesystem::path& user_directory)
        -> std::vector<KeyboardLayout>;

    [[nodiscard]] auto All() -> const std::vector<KeyboardLayout>&;
    [[nodiscard]] auto Find(std::string_view name) -> const KeyboardLayout&;
    [[nodiscard]] auto After(std::string_view name) -> const KeyboardLayout&;
    auto Reload() -> void;

  private:
    std::optional<std::vector<KeyboardLayout>> m_profiles;
};

extern KeyboardProfiles* g_keyboard_profiles;
