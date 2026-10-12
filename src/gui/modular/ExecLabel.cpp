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

#include "ExecLabel.hpp"

#include <format>

#include "Toast.hpp"

ExecLabel::ExecLabel(std::vector<std::string> command, float interval)
    : m_command(std::move(command)),
      m_interval(interval),
      m_last_exec(std::chrono::steady_clock::now()) {
}

auto ExecLabel::Update() -> void {
    if (m_child) {
        const auto status = m_child->TryWait();
        if (!status) {
            m_child.reset();
            ToastOverlay::ShowError(std::format("Error checking child process: {}", status.error().Message()));
            return;
        }
        if (!status->has_value()) {
            return;
        }
        auto child = std::move(*m_child);
        m_child.reset();
        if ((*status)->Success()) {
            if (const auto output = child.ReadStdout()) {
                SetText(*output);
            } else {
                ToastOverlay::ShowError("LabelData::Exec: Failed to read stdout for child process");
            }
            return;
        }
        ToastOverlay::ShowError(std::format("LabelData::Exec: Child process exited with code: {}", (*status)->Describe()));
    }

    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<float>(now - m_last_exec).count() <= m_interval) {
        return;
    }
    m_last_exec = now;
    if (auto process = Process::Spawn(m_command, {.capture_stdout = true})) {
        m_child.emplace(std::move(*process));
    } else {
        ToastOverlay::ShowError(std::format("Failed to spawn process {}: {}", m_command, process.error().Message()));
    }
}
