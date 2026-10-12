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

#include "ExitStatus.hpp"

#include <cstring>
#include <format>

auto ExitStatus::Describe() const -> std::string {
    if (m_signal) {
        if (const char* name = sigabbrev_np(*m_signal)) {
            return std::format("signal: {} (SIG{})", *m_signal, name);
        }
        return std::format("signal: {}", *m_signal);
    }
    return std::format("exit status: {}", m_code);
}
