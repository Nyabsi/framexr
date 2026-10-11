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
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/types.h>
#endif

#include "Error.hpp"

struct ProcessOptions {
    std::vector<std::pair<std::string, std::string>> set_env;
    std::vector<std::string> unset_env;
    bool capture_stdout = false;
};

struct ExitStatus {
    int code = 0;
    std::optional<int> signal;

    [[nodiscard]] auto Success() const -> bool { return !signal && code == 0; }
    [[nodiscard]] auto Describe() const -> std::string;
};

class Process final {
  public:
    static auto Spawn(const std::vector<std::string>& command, const ProcessOptions& options = {}) -> Result<Process>;

    ~Process();
    Process(Process&& other) noexcept;
    auto operator=(Process&& other) noexcept -> Process&;
    Process(const Process&) = delete;
    auto operator=(const Process&) -> Process& = delete;

    [[nodiscard]] auto Pid() const -> uint32_t;

    auto TryWait() -> Result<std::optional<ExitStatus>>;
    auto Signal(int signal) const -> void;
    auto ReadStdout() -> std::optional<std::string>;

  private:
#ifdef _WIN32
    Process(void* process, void* stdout_pipe);

    void* process_ = nullptr;
    void* stdout_pipe_ = nullptr;
#else
    Process(pid_t pid, int stdout_fd);

    pid_t pid_ = -1;
    int stdout_fd_ = -1;
#endif
    std::optional<ExitStatus> exit_status_;
};
