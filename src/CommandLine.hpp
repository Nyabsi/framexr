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

#include <optional>
#include <span>
#include <string>
#include <variant>

struct CommandLine {
    bool openvr = false;
    bool openxr = false;
    bool show = false;
    bool uninstall = false;
    bool replace = false;
    bool multi = false;
    std::optional<std::string> log_to;

    static auto Parse(std::span<char*> arguments) -> std::variant<CommandLine, int>;
    static auto Usage() -> std::string;
    static auto Help() -> std::string;
};
