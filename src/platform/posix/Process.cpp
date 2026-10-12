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

#include "Process.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <format>
#include <utility>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {

auto BuildEnvironment(const ProcessOptions& options) -> std::vector<std::string> {
    std::vector<std::string> environment;
    for (char** entry = environ; *entry != nullptr; ++entry) {
        const std::string_view variable(*entry);
        const auto name = variable.substr(0, variable.find('='));
        const auto overridden = std::ranges::any_of(options.set_env, [&](const auto& pair) -> bool { return pair.first == name; });
        const auto removed = std::ranges::find(options.unset_env, name) != options.unset_env.end();
        if (!overridden && !removed) {
            environment.emplace_back(variable);
        }
    }
    for (const auto& [name, value] : options.set_env) {
        environment.push_back(std::format("{}={}", name, value));
    }
    return environment;
}

auto ToPointers(std::vector<std::string>& strings) -> std::vector<char*> {
    std::vector<char*> pointers;
    pointers.reserve(strings.size() + 1);
    for (auto& string : strings) {
        pointers.push_back(string.data());
    }
    pointers.push_back(nullptr);
    return pointers;
}

} // namespace

auto Process::Spawn(const std::vector<std::string>& command, const ProcessOptions& options) -> Result<Process> {
    if (command.empty()) {
        return Unexpected("empty command");
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    int pipe_fds[2] = {-1, -1};
    if (options.capture_stdout) {
        if (pipe2(pipe_fds, O_CLOEXEC) != 0) {
            posix_spawn_file_actions_destroy(&actions);
            return Unexpected(std::format("{} (os error {})", std::strerror(errno), errno));
        }
        posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    }

    auto arguments = command;
    auto argument_pointers = ToPointers(arguments);
    auto environment = BuildEnvironment(options);
    auto environment_pointers = ToPointers(environment);

    pid_t pid = -1;
    const int error = posix_spawnp(
        &pid,
        arguments.front().c_str(),
        &actions,
        nullptr,
        argument_pointers.data(),
        environment_pointers.data()
    );
    posix_spawn_file_actions_destroy(&actions);

    if (options.capture_stdout) {
        close(pipe_fds[1]);
    }
    if (error != 0) {
        if (options.capture_stdout) {
            close(pipe_fds[0]);
        }
        return Unexpected(std::format("{} (os error {})", std::strerror(error), error));
    }
    return Process(pid, pipe_fds[0]);
}

Process::Process(pid_t pid, int stdout_fd) : m_pid(pid), m_stdout_fd(stdout_fd) {
}

auto Process::Pid() const -> uint32_t {
    return static_cast<uint32_t>(m_pid);
}

Process::~Process() {
    if (m_stdout_fd >= 0) {
        close(m_stdout_fd);
    }
}

Process::Process(Process&& other) noexcept
    : m_pid(std::exchange(other.m_pid, -1)),
      m_stdout_fd(std::exchange(other.m_stdout_fd, -1)),
      m_exit_status(other.m_exit_status) {
}

auto Process::operator=(Process&& other) noexcept -> Process& {
    if (this != &other) {
        if (m_stdout_fd >= 0) {
            close(m_stdout_fd);
        }
        m_pid = std::exchange(other.m_pid, -1);
        m_stdout_fd = std::exchange(other.m_stdout_fd, -1);
        m_exit_status = other.m_exit_status;
    }
    return *this;
}

auto Process::TryWait() -> Result<std::optional<ExitStatus>> {
    if (m_exit_status) {
        return m_exit_status;
    }
    int status = 0;
    const pid_t result = waitpid(m_pid, &status, WNOHANG);
    if (result < 0) {
        return Unexpected(std::format("{} (os error {})", std::strerror(errno), errno));
    }
    if (result == 0) {
        return std::nullopt;
    }
    if (WIFSIGNALED(status)) {
        m_exit_status = ExitStatus(0, WTERMSIG(status));
    } else {
        m_exit_status = ExitStatus(WEXITSTATUS(status));
    }
    return m_exit_status;
}

auto Process::Signal(int signal) const -> void {
    if (m_pid > 0) {
        kill(m_pid, signal);
    }
}

auto Process::ReadStdout() -> std::optional<std::string> {
    if (m_stdout_fd < 0) {
        return std::nullopt;
    }
    std::string output;
    char buffer[4096];
    while (true) {
        const auto count = read(m_stdout_fd, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::nullopt;
        }
        if (count == 0) {
            break;
        }
        output.append(buffer, static_cast<size_t>(count));
    }
    close(m_stdout_fd);
    m_stdout_fd = -1;
    return output;
}
