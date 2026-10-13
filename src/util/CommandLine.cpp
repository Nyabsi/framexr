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

#include "CommandLine.hpp"

#include <cstdio>
#include <format>
#include <string_view>

#include "framexr_config.h"

CommandLine* g_command_line = nullptr;

auto CommandLine::PrintError(std::string_view message) -> int {
    std::fputs(std::format("error: {}\n\n{}\n\nFor more information, try '--help'.\n", message, Usage()).c_str(), stderr);
    return 2;
}

auto CommandLine::Usage() -> std::string {
    return std::format("Usage: {} [OPTIONS]", FRAMEXR_PKG_NAME);
}

auto CommandLine::Help() -> std::string {
    std::string help = std::format("The lightweight desktop overlay for OpenVR and OpenXR\n\n{}\n\nOptions:\n", Usage());
#if FRAMEXR_FEATURE_OPENVR
    help += "      --openvr               Force OpenVR backend\n";
#endif
#if FRAMEXR_FEATURE_OPENXR
    help += "      --openxr               Force OpenXR backend\n";
#endif
    help += "      --show                 Show the working set of overlay on startup\n"
            "      --uninstall            Uninstall OpenVR manifest and exit\n"
            "      --replace              Replace running FrameXR instance\n"
            "      --multi                Allow multiple running instances of FrameXR (things may break!)\n"
            "  -l, --log-to <FILE_PATH>   Path to write logs to\n";
    help += "  -h, --help                 Print help\n"
            "  -V, --version              Print version\n";
    return help;
}

auto CommandLine::Parse(std::span<char*> arguments) -> std::optional<int> {
    const auto args = arguments.subspan(arguments.empty() ? 0 : 1);

    bool any_non_empty = false;
    for (const char* arg : args) {
        any_non_empty = any_non_empty || *arg != '\0';
    }
    if (!any_non_empty) {
        return std::nullopt;
    }

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];

        const auto take_value =
            [&](std::string_view long_name, std::string_view value_name, std::optional<std::string>& target) -> std::optional<int> {
            std::string_view value;
            if (const auto equals = arg.find('='); arg.starts_with("--") && equals != std::string_view::npos) {
                value = arg.substr(equals + 1);
            } else if (!arg.starts_with("--") && arg.size() > 2) {
                value = arg.substr(2);
            } else if (i + 1 < args.size()) {
                value = args[++i];
            } else {
                return PrintError(std::format("a value is required for '--{} <{}>' but none was supplied", long_name, value_name));
            }
            target = std::string(value);
            return std::nullopt;
        };

        if (arg == "-h" || arg == "--help") {
            std::fputs(Help().c_str(), stdout);
            return 0;
        }
        if (arg == "-V" || arg == "--version") {
            std::fputs(std::format("{} {}\n", FRAMEXR_PKG_NAME, FRAMEXR_PKG_VERSION).c_str(), stdout);
            return 0;
        }
#if FRAMEXR_FEATURE_OPENVR
        if (arg == "--openvr") {
            m_openvr = true;
            continue;
        }
#endif
#if FRAMEXR_FEATURE_OPENXR
        if (arg == "--openxr") {
            m_openxr = true;
            continue;
        }
#endif
        if (arg == "--show") {
            m_show = true;
        } else if (arg == "--uninstall") {
            m_uninstall = true;
        } else if (arg == "--replace") {
            m_replace = true;
        } else if (arg == "--multi") {
            m_multi = true;
        } else if (arg == "--log-to" || arg.starts_with("--log-to=") || arg.starts_with("-l")) {
            if (auto error = take_value("log-to", "FILE_PATH", m_log_to)) {
                return error;
            }
        } else {
            return PrintError(std::format("unexpected argument '{}' found", arg));
        }
    }
    return std::nullopt;
}
