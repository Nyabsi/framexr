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
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <optional>
#include <string>

#include "Error.hpp"

class Platform final
{
  public:
    static auto Init() -> void;
    static auto ConfigHome() -> std::optional<std::filesystem::path>;
    static auto Locale() -> std::string;
    static auto RuntimeDirectory() -> std::filesystem::path;
    static auto DefaultLogFile() -> std::filesystem::path;
    static auto OpenLogFile(const std::filesystem::path& path) -> Result<std::FILE*>;
    static auto ExecutablePath() -> Result<std::filesystem::path>;
    static auto IsTerminal(std::FILE* stream) -> bool;
    static auto LocalTime(std::time_t time) -> std::tm;
    static auto OnTerminate(void (*handler)()) -> void;
    static auto ProcessId() -> uint32_t;
    static auto ProcessAlive(uint32_t pid) -> bool;
    static auto Terminate(uint32_t pid) -> void;
};
