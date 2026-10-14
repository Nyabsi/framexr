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

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string_view>

namespace {

void (*terminate_handler)() = nullptr;

auto OnSignal(int) -> void {
    if (terminate_handler != nullptr) {
        terminate_handler();
    }
}

auto IoErrorKind(int error) -> std::string_view {
    switch (error) {
    case ENOENT: return "NotFound";
    case EACCES:
    case EPERM: return "PermissionDenied";
    case EISDIR: return "IsADirectory";
    case EROFS: return "ReadOnlyFilesystem";
    case ENOTDIR: return "NotADirectory";
    case ENOSPC: return "StorageFull";
    default: return "Uncategorized";
    }
}

} // namespace

auto Platform::Init() -> void {
}

auto Platform::ConfigHome() -> std::optional<std::filesystem::path> {
    if (const char* config_home = std::getenv("XDG_CONFIG_HOME"); config_home != nullptr && *config_home == '/') {
        return std::filesystem::path(config_home);
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config";
    }
    return std::nullopt;
}

auto Platform::Locale() -> std::string {
    for (const char* name : {"LC_ALL", "LC_CTYPE", "LANG"}) {
        if (const char* value = std::getenv(name); value != nullptr && *value != '\0') {
            return value;
        }
    }
    return {};
}

auto Platform::RuntimeDirectory() -> std::filesystem::path {
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    return runtime_dir != nullptr ? std::filesystem::path(runtime_dir) : std::filesystem::path("/tmp");
}

auto Platform::DefaultLogFile() -> std::filesystem::path {
    return "/tmp/framexr.log";
}

auto Platform::OpenLogFile(const std::filesystem::path& path) -> Result<std::FILE*> {
    const int fd = open(path.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC);
    if (std::FILE* file = fd >= 0 ? fdopen(fd, "w") : nullptr) {
        return file;
    }
    const int error = errno;
    if (fd >= 0) {
        close(fd);
    }
    return Unexpected(std::format("Os {{ code: {}, kind: {}, message: \"{}\" }}", error, IoErrorKind(error), std::strerror(error)));
}

auto Platform::ExecutablePath() -> Result<std::filesystem::path> {
    if (const char* appimage = std::getenv("APPIMAGE")) {
        return std::filesystem::path(appimage);
    }
    std::error_code error;
    auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) {
        return Unexpected(std::format("Invalid executable path: {}", error.message()));
    }
    return path;
}

auto Platform::IsTerminal(std::FILE* stream) -> bool {
    return isatty(fileno(stream)) != 0;
}

auto Platform::LocalTime(std::time_t time) -> std::tm {
    std::tm local{};
    localtime_r(&time, &local);
    return local;
}

auto Platform::OnTerminate(void (*handler)()) -> void {
    terminate_handler = handler;
    struct sigaction action{};
    action.sa_handler = OnSignal;
    sigemptyset(&action.sa_mask);
    for (const int signal : {SIGINT, SIGTERM, SIGHUP}) {
        sigaction(signal, &action, nullptr);
    }
}

auto Platform::ProcessId() -> uint32_t {
    return static_cast<uint32_t>(getpid());
}

auto Platform::ProcessAlive(uint32_t pid) -> bool {
    return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}

auto Platform::Terminate(uint32_t pid) -> void {
    kill(static_cast<pid_t>(pid), SIGTERM);
}
