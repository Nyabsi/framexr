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

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <thread>
#include <variant>

#include <fcntl.h>
#include <unistd.h>

#include "Backend.hpp"
#include "CommandLine.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include "version.h"
#include "framexr_config.h"

#if FRAMEXR_FEATURE_OPENVR
#include "openvr/OpenVrBackend.hpp"
#endif

#if FRAMEXR_FEATURE_OPENXR
#include "openxr/OpenXrBackend.hpp"
#endif

#if FRAMEXR_FEATURE_UIDEV
#include "uidev/UiDev.hpp"
#endif

#if !FRAMEXR_FEATURE_OPENVR && !FRAMEXR_FEATURE_OPENXR
#error "No VR support! Enable either openvr or openxr features!"
#endif

#if !FRAMEXR_FEATURE_WAYLAND && !FRAMEXR_FEATURE_X11
#error "No desktop support! Enable either wayland or x11 features!"
#endif

namespace {

std::atomic<bool> running{true};

auto OnSignal(int) -> void {
    running.store(false, std::memory_order_relaxed);
}

auto InstallSignalHandlers() -> void {
    struct sigaction action{};
    action.sa_handler = OnSignal;
    sigemptyset(&action.sa_mask);
    for (const int signal : {SIGINT, SIGTERM, SIGHUP}) {
        sigaction(signal, &action, nullptr);
    }
}

auto ProcessAlive(pid_t pid) -> bool {
    return kill(pid, 0) == 0 || errno == EPERM;
}

auto EnsureSingleInstance(bool replace) -> bool {
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    const auto path = std::filesystem::path(runtime_dir != nullptr ? runtime_dir : "/tmp") / "framexr.pid";

    if (const auto contents = Config::ReadFile(path)) {
        char* end = nullptr;
        const auto pid = std::strtol(contents->c_str(), &end, 10);
        if (end != contents->c_str() && pid > 0 && ProcessAlive(static_cast<pid_t>(pid))) {
            if (!replace) {
                return false;
            }
            kill(static_cast<pid_t>(pid), SIGTERM);
            while (ProcessAlive(static_cast<pid_t>(pid))) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    std::ofstream stream(path, std::ios::trunc);
    stream << getpid();
    if (!stream) {
        FatalError(std::format("Failed to write {}", path.string()));
    }
    return true;
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

auto InitLogging(const CommandLine& command_line) -> void {
    std::string path = "/tmp/framexr.log";
    if (command_line.log_to) {
        path = *command_line.log_to;
    } else if (const char* env = std::getenv("FRAMEXR_LOGFILE")) {
        path = env;
    }

    const int fd = open(path.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC);
    FILE* file = fd >= 0 ? fdopen(fd, "w") : nullptr;
    if (file != nullptr) {
        std::printf("Logging to %s\n", path.c_str());
        std::fflush(stdout);
        Logger::Instance().SetOutputFile(file);
    } else {
        const int error = errno;
        std::fputs(
            std::format(
                "Failed to open log file (path: Os {{ code: {}, kind: {}, message: \"{}\" }}): {}\n",
                error,
                IoErrorKind(error),
                std::strerror(error),
                path
            )
                .c_str(),
            stdout
        );
        std::fflush(stdout);
        if (fd >= 0) {
            close(fd);
        }
    }
}

auto BuildVersion() -> std::string {
    std::string describe = FRAMEXR_GIT_DESCRIBE;
    if (!describe.starts_with('v') && !describe.ends_with("-unknown")) {
        describe = std::format("{}-{}", FRAMEXR_PKG_VERSION, describe);
    }

    if (describe.starts_with('v')) {
        const auto version_end = describe.find_first_not_of("0123456789.", 1);
        const auto count_end = version_end != std::string::npos && describe[version_end] == '-'
                                 ? describe.find_first_not_of("0123456789", version_end + 1)
                                 : std::string::npos;
        if (count_end != std::string::npos && count_end > version_end + 1 && describe.compare(count_end, 2, "-g") == 0) {
            const auto hash_end = describe.find_first_not_of("0123456789abcdef", count_end + 2);
            const auto hash = describe.substr(
                count_end + 2,
                hash_end == std::string::npos ? std::string::npos : hash_end - count_end - 2
            );
            if (!hash.empty()) {
                describe = std::format(
                    "{}.r{}.{}{}",
                    describe.substr(1, version_end - 1),
                    describe.substr(version_end + 1, count_end - version_end - 1),
                    hash,
                    hash_end == std::string::npos ? "" : describe.substr(hash_end)
                );
            }
        }
    }
    return describe + FRAMEXR_BUILD_SUFFIX;
}

auto LocalTime() -> std::string {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::array<char, 64> buffer{};
    const auto length = std::strftime(buffer.data(), buffer.size(), "%a %b %e %H:%M:%S %Y", &local);
    return {buffer.data(), length};
}

auto HandleResult(const BackendResult& result) -> bool {
    if (result) {
        return true;
    }
    if (result.error().failure == BackendFailure::NotSupported) {
        return false;
    }
    LOG_ERROR("Fatal: {}", result.error().message);
    return true;
}

auto AutoRun(const CommandLine& command_line) -> void {
#if FRAMEXR_FEATURE_OPENXR
    if (!command_line.openvr && HandleResult(OpenXrBackend::Run(running, command_line.show))) {
        return;
    }
#endif

#if FRAMEXR_FEATURE_OPENVR
    if (!command_line.openxr && HandleResult(OpenVrBackend::Run(running, command_line.show))) {
        return;
    }
#endif

    LOG_ERROR("No more backends to try");
}

auto Run(std::span<char*> arguments) -> int {
    auto parsed = CommandLine::Parse(arguments);
    if (const auto* exit_code = std::get_if<int>(&parsed)) {
        return *exit_code;
    }
    const auto& command_line = std::get<CommandLine>(parsed);

    if (!command_line.multi && !EnsureSingleInstance(command_line.replace)) {
        std::puts("Looks like FrameXR is already running.");
        std::puts("Use --replace and I will terminate it for you.");
        return 0;
    }

    InitLogging(command_line);

    LOG_INFO("Welcome to {} version {}!", FRAMEXR_PKG_NAME, BuildVersion());
    LOG_INFO("It is {}.", LocalTime());

#if FRAMEXR_FEATURE_OPENVR
    if (command_line.uninstall) {
        OpenVrBackend::Uninstall();
        return 0;
    }
#endif

#if FRAMEXR_FEATURE_UIDEV
    if (command_line.uidev) {
        if (const auto result = UiDev::Run(*command_line.uidev); !result) {
            LOG_ERROR("{}", result.error().Message());
            return 1;
        }
        return 0;
    }
#endif

    InstallSignalHandlers();
    AutoRun(command_line);
    return 0;
}

} // namespace

auto main(int argc, char** argv) -> int {
    try {
        return Run(std::span(argv, static_cast<size_t>(argc)));
    } catch (const std::exception& exception) {
        FatalError(exception.what());
    }
}
