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

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Error.hpp"
#include "document/FormDocument.hpp"

class Forms final
{
  public:
    static constexpr std::string_view kDefaultWatch = "default";

    static auto Load(const std::string& name) -> Result<FormDocument>;
    static auto Watch(std::string_view profile) -> FormDocument;
    static auto WatchFile(std::string_view profile) -> std::filesystem::path;
    static auto WatchProfiles() -> std::vector<std::string>;
    static auto SaveWatch(std::string_view profile, const FormDocument& document) -> Result<>;
    static auto DeleteWatch(std::string_view profile) -> void;
    static auto IsBuiltInWatch(std::string_view profile) -> bool;
};
