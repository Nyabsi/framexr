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

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <format>
#include <utility>

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

auto ExitStatus::Describe() const -> std::string {
    if (signal) {
        if (const char* name = sigabbrev_np(*signal)) {
            return std::format("signal: {} (SIG{})", *signal, name);
        }
        return std::format("signal: {}", *signal);
    }
    return std::format("exit status: {}", code);
}

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

Process::Process(pid_t pid, int stdout_fd) : pid_(pid), stdout_fd_(stdout_fd) {
}

Process::~Process() {
    if (stdout_fd_ >= 0) {
        close(stdout_fd_);
    }
}

Process::Process(Process&& other) noexcept
    : pid_(std::exchange(other.pid_, -1)),
      stdout_fd_(std::exchange(other.stdout_fd_, -1)),
      exit_status_(other.exit_status_) {
}

auto Process::operator=(Process&& other) noexcept -> Process& {
    if (this != &other) {
        if (stdout_fd_ >= 0) {
            close(stdout_fd_);
        }
        pid_ = std::exchange(other.pid_, -1);
        stdout_fd_ = std::exchange(other.stdout_fd_, -1);
        exit_status_ = other.exit_status_;
    }
    return *this;
}

auto Process::TryWait() -> Result<std::optional<ExitStatus>> {
    if (exit_status_) {
        return exit_status_;
    }
    int status = 0;
    const pid_t result = waitpid(pid_, &status, WNOHANG);
    if (result < 0) {
        return Unexpected(std::format("{} (os error {})", std::strerror(errno), errno));
    }
    if (result == 0) {
        return std::nullopt;
    }
    if (WIFSIGNALED(status)) {
        exit_status_ = ExitStatus{.code = 0, .signal = WTERMSIG(status)};
    } else {
        exit_status_ = ExitStatus{.code = WEXITSTATUS(status)};
    }
    return exit_status_;
}

auto Process::Signal(int signal) const -> void {
    if (pid_ > 0) {
        kill(pid_, signal);
    }
}

auto Process::ReadStdout() -> std::optional<std::string> {
    if (stdout_fd_ < 0) {
        return std::nullopt;
    }
    std::string output;
    char buffer[4096];
    while (true) {
        const auto count = read(stdout_fd_, buffer, sizeof(buffer));
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
    close(stdout_fd_);
    stdout_fd_ = -1;
    return output;
}
