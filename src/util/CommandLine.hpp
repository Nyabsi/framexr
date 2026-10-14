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
#include <string_view>

class CommandLine final
{
  public:
    auto Parse(std::span<char*> arguments) -> std::optional<int>;

    [[nodiscard]] auto OpenVr() const -> bool { return m_openvr; }
    [[nodiscard]] auto OpenXr() const -> bool { return m_openxr; }
    [[nodiscard]] auto Show() const -> bool { return m_show; }
    [[nodiscard]] auto Uninstall() const -> bool { return m_uninstall; }
    [[nodiscard]] auto Replace() const -> bool { return m_replace; }
    [[nodiscard]] auto Multi() const -> bool { return m_multi; }
    [[nodiscard]] auto LogTo() const -> const std::optional<std::string>& { return m_log_to; }

  private:
    static auto Usage() -> std::string;
    static auto Help() -> std::string;
    static auto PrintError(std::string_view message) -> int;

    bool m_openvr = false;
    bool m_openxr = false;
    bool m_show = false;
    bool m_uninstall = false;
    bool m_replace = false;
    bool m_multi = false;
    std::optional<std::string> m_log_to;
};

extern CommandLine* g_command_line;
