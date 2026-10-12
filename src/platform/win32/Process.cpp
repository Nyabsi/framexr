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
#include <format>
#include <string_view>
#include <system_error>
#include <utility>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

auto Widen(std::string_view text) -> std::wstring {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

auto LastError() -> std::string {
    const auto error = GetLastError();
    return std::format("{} (os error {})", std::system_category().message(static_cast<int>(error)), error);
}

auto Quote(const std::wstring& argument) -> std::wstring {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        quoted.append(character == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(character);
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

auto CommandLine(const std::vector<std::string>& command) -> std::wstring {
    std::wstring line;
    for (const auto& argument : command) {
        if (!line.empty()) {
            line.push_back(L' ');
        }
        line += Quote(Widen(argument));
    }
    return line;
}

auto Environment(const ProcessOptions& options) -> std::wstring {
    std::wstring block;
    if (wchar_t* strings = GetEnvironmentStringsW()) {
        for (const wchar_t* entry = strings; *entry != L'\0'; entry += std::wstring_view(entry).size() + 1) {
            const std::wstring_view variable(entry);
            const auto name = variable.substr(0, variable.find(L'=', 1));
            const auto overridden = std::ranges::any_of(options.set_env, [&](const auto& pair) -> bool {
                return Widen(pair.first) == name;
            });
            const auto removed = std::ranges::any_of(options.unset_env, [&](const auto& unset) -> bool {
                return Widen(unset) == name;
            });
            if (!overridden && !removed) {
                block.append(variable).push_back(L'\0');
            }
        }
        FreeEnvironmentStringsW(strings);
    }
    for (const auto& [name, value] : options.set_env) {
        block.append(Widen(name)).append(L"=").append(Widen(value)).push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

} // namespace

auto Process::Spawn(const std::vector<std::string>& command, const ProcessOptions& options) -> Result<Process> {
    if (command.empty()) {
        return Unexpected("empty command");
    }

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (options.capture_stdout) {
        SECURITY_ATTRIBUTES attributes{
            .nLength = sizeof(SECURITY_ATTRIBUTES),
            .lpSecurityDescriptor = nullptr,
            .bInheritHandle = TRUE
        };
        if (CreatePipe(&read_pipe, &write_pipe, &attributes, 0) == 0) {
            return Unexpected(LastError());
        }
        SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOW startup{.cb = sizeof(STARTUPINFOW)};
    if (options.capture_stdout) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = write_pipe;
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    }

    auto command_line = CommandLine(command);
    auto environment = Environment(options);
    PROCESS_INFORMATION information{};
    const BOOL created = CreateProcessW(
        nullptr,
        command_line.data(),
        nullptr,
        nullptr,
        options.capture_stdout ? TRUE : FALSE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
        environment.data(),
        nullptr,
        &startup,
        &information
    );
    const auto error = created == 0 ? LastError() : std::string();
    if (write_pipe != nullptr) {
        CloseHandle(write_pipe);
    }
    if (created == 0) {
        if (read_pipe != nullptr) {
            CloseHandle(read_pipe);
        }
        return Unexpected(error);
    }
    CloseHandle(information.hThread);
    return Process(information.hProcess, read_pipe);
}

Process::Process(void* process, void* stdout_pipe) : m_process(process), m_stdout_pipe(stdout_pipe) {
}

Process::~Process() {
    if (m_stdout_pipe != nullptr) {
        CloseHandle(m_stdout_pipe);
    }
    if (m_process != nullptr) {
        CloseHandle(m_process);
    }
}

Process::Process(Process&& other) noexcept
    : m_process(std::exchange(other.m_process, nullptr)),
      m_stdout_pipe(std::exchange(other.m_stdout_pipe, nullptr)),
      m_exit_status(other.m_exit_status) {
}

auto Process::operator=(Process&& other) noexcept -> Process& {
    if (this != &other) {
        if (m_stdout_pipe != nullptr) {
            CloseHandle(m_stdout_pipe);
        }
        if (m_process != nullptr) {
            CloseHandle(m_process);
        }
        m_process = std::exchange(other.m_process, nullptr);
        m_stdout_pipe = std::exchange(other.m_stdout_pipe, nullptr);
        m_exit_status = other.m_exit_status;
    }
    return *this;
}

auto Process::Pid() const -> uint32_t {
    return m_process != nullptr ? GetProcessId(m_process) : 0;
}

auto Process::TryWait() -> Result<std::optional<ExitStatus>> {
    if (m_exit_status) {
        return m_exit_status;
    }
    const DWORD waited = WaitForSingleObject(m_process, 0);
    if (waited == WAIT_TIMEOUT) {
        return std::nullopt;
    }
    if (waited != WAIT_OBJECT_0) {
        return Unexpected(LastError());
    }
    DWORD code = 0;
    if (GetExitCodeProcess(m_process, &code) == 0) {
        return Unexpected(LastError());
    }
    m_exit_status = ExitStatus(static_cast<int>(code));
    return m_exit_status;
}

auto Process::Signal(int) const -> void {
    if (m_process != nullptr) {
        TerminateProcess(m_process, 1);
    }
}

auto Process::ReadStdout() -> std::optional<std::string> {
    if (m_stdout_pipe == nullptr) {
        return std::nullopt;
    }
    std::string output;
    char buffer[4096];
    DWORD count = 0;
    while (ReadFile(m_stdout_pipe, buffer, sizeof(buffer), &count, nullptr) != 0 && count > 0) {
        output.append(buffer, count);
    }
    CloseHandle(m_stdout_pipe);
    m_stdout_pipe = nullptr;
    return output;
}
