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

#include "Platform.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <io.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <array>
#include <cstdlib>
#include <format>
#include <system_error>

namespace {

void (*terminate_handler)() = nullptr;

auto WINAPI OnConsoleEvent(DWORD event) -> BOOL {
    switch (event) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        if (terminate_handler != nullptr) {
            terminate_handler();
        }
        return TRUE;
    default: return FALSE;
    }
}

auto LastError() -> std::string {
    return std::system_category().message(static_cast<int>(GetLastError()));
}

} // namespace

auto Platform::Init() -> void {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetConsoleOutputCP(CP_UTF8);
}

auto Platform::ConfigHome() -> std::optional<std::filesystem::path> {
    if (const char* config_home = std::getenv("XDG_CONFIG_HOME"); config_home != nullptr && *config_home != '\0') {
        return std::filesystem::path(config_home);
    }
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &folder))) {
        CoTaskMemFree(folder);
        return std::nullopt;
    }
    std::filesystem::path path(folder);
    CoTaskMemFree(folder);
    return path;
}

auto Platform::Locale() -> std::string {
    std::array<wchar_t, LOCALE_NAME_MAX_LENGTH> name{};
    const int length = GetUserDefaultLocaleName(name.data(), static_cast<int>(name.size()));
    return length > 1 ? std::filesystem::path(std::wstring(name.data(), static_cast<size_t>(length - 1))).string() : std::string();
}

auto Platform::RuntimeDirectory() -> std::filesystem::path {
    std::error_code error;
    auto path = std::filesystem::temp_directory_path(error);
    return error ? std::filesystem::path(".") : path;
}

auto Platform::DefaultLogFile() -> std::filesystem::path {
    return RuntimeDirectory() / "framexr.log";
}

auto Platform::OpenLogFile(const std::filesystem::path& path) -> Result<std::FILE*> {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return Unexpected(std::format("{} does not exist", path.string()));
    }
    if (std::FILE* file = _wfopen(path.c_str(), L"w")) {
        return file;
    }
    return Unexpected(std::format("Could not open {}", path.string()));
}

auto Platform::ExecutablePath() -> Result<std::filesystem::path> {
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return Unexpected(std::format("Invalid executable path: {}", LastError()));
    }
    return std::filesystem::path(std::wstring(buffer.data(), length));
}

auto Platform::IsTerminal(std::FILE* stream) -> bool {
    if (_isatty(_fileno(stream)) == 0) {
        return false;
    }
    HANDLE handle = GetStdHandle(stream == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    DWORD mode = 0;
    return GetConsoleMode(handle, &mode) != 0 && SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}

auto Platform::LocalTime(std::time_t time) -> std::tm {
    std::tm local{};
    localtime_s(&local, &time);
    return local;
}

auto Platform::OnTerminate(void (*handler)()) -> void {
    terminate_handler = handler;
    SetConsoleCtrlHandler(OnConsoleEvent, TRUE);
}

auto Platform::ProcessId() -> uint32_t {
    return GetCurrentProcessId();
}

auto Platform::ProcessAlive(uint32_t pid) -> bool {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return GetLastError() == ERROR_ACCESS_DENIED;
    }
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(process, &code) != 0 && code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
}

auto Platform::Terminate(uint32_t pid) -> void {
    if (HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid)) {
        TerminateProcess(process, 1);
        CloseHandle(process);
    }
}
