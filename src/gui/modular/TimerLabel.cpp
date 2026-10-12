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

#include "TimerLabel.hpp"

#include <algorithm>
#include <format>

TimerLabel::TimerLabel(std::string format) : m_format(std::move(format)), m_start(std::chrono::steady_clock::now()) {
}

auto TimerLabel::Update() -> void {
    auto formatted = m_format;
    std::ranges::transform(formatted, formatted.begin(), [](unsigned char c) -> char {
        return static_cast<char>(std::tolower(c));
    });
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_start).count();
    const auto replace_all = [&](std::string_view token, const std::string& replacement) -> void {
        for (auto position = formatted.find(token); position != std::string::npos;
             position = formatted.find(token, position + replacement.size())) {
            formatted.replace(position, token.size(), replacement);
        }
    };
    replace_all("%s", std::format("{:02}", elapsed % 60));
    replace_all("%m", std::format("{:02}", (elapsed / 60) % 60));
    replace_all("%h", std::format("{:02}", (elapsed / 60) / 60));
    SetText(formatted);
}
