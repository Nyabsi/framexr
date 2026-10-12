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
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include "Backend.hpp"
#include "CommandLine.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include "Platform.hpp"
#include "framexr_config.h"
#include "version.h"

#if FRAMEXR_FEATURE_OPENVR
#include "openvr/OpenVrBackend.hpp"
#endif

#if FRAMEXR_FEATURE_OPENXR
#include "openxr/OpenXrBackend.hpp"
#endif

#if !FRAMEXR_FEATURE_OPENVR && !FRAMEXR_FEATURE_OPENXR
#error "No VR support! Enable either openvr or openxr features!"
#endif

#if !FRAMEXR_FEATURE_WAYLAND && !FRAMEXR_FEATURE_X11 && !defined(_WIN32)
#error "No desktop support! Enable either wayland or x11 features!"
#endif

namespace {

std::atomic<bool> running{true};

auto OnTerminate() -> void {
    running.store(false, std::memory_order_relaxed);
}

auto EnsureSingleInstance(bool replace) -> bool {
    const auto path = Platform::RuntimeDirectory() / "framexr.pid";

    if (const auto contents = ConfigStore::ReadFile(path)) {
        char* end = nullptr;
        const auto pid = std::strtoul(contents->c_str(), &end, 10);
        if (end != contents->c_str() && pid > 0 && Platform::ProcessAlive(static_cast<uint32_t>(pid))) {
            if (!replace) {
                return false;
            }
            Platform::Terminate(static_cast<uint32_t>(pid));
            while (Platform::ProcessAlive(static_cast<uint32_t>(pid))) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    std::ofstream stream(path, std::ios::trunc);
    stream << Platform::ProcessId();
    if (!stream) {
        FatalError(std::format("Failed to write {}", path.string()));
    }
    return true;
}

auto InitLogging() -> void {
    std::filesystem::path path = Platform::DefaultLogFile();
    if (g_command_line->LogTo()) {
        path = *g_command_line->LogTo();
    } else if (const char* env = std::getenv("FRAMEXR_LOGFILE")) {
        path = env;
    }

    if (auto file = Platform::OpenLogFile(path)) {
        std::printf("Logging to %s\n", path.string().c_str());
        std::fflush(stdout);
        g_logger->SetOutputFile(*file);
    } else {
        std::fputs(std::format("Failed to open log file (path: {}): {}\n", file.error().Message(), path.string()).c_str(), stdout);
        std::fflush(stdout);
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
    const std::tm local = Platform::LocalTime(std::time(nullptr));
    std::array<char, 64> buffer{};
    const auto date_length = std::strftime(buffer.data(), buffer.size(), "%a %b", &local);
    const auto time_length = std::strftime(buffer.data() + date_length, buffer.size() - date_length, " %H:%M:%S %Y", &local);
    return std::format(
        "{} {:>2}{}",
        std::string_view(buffer.data(), date_length),
        local.tm_mday,
        std::string_view(buffer.data() + date_length, time_length)
    );
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

auto AutoRun() -> void {
#if FRAMEXR_FEATURE_OPENXR
    if (!g_command_line->OpenVr() && HandleResult(OpenXrBackend::Run(running, g_command_line->Show()))) {
        return;
    }
#endif

#if FRAMEXR_FEATURE_OPENVR
    if (!g_command_line->OpenXr() && HandleResult(OpenVrBackend::Run(running, g_command_line->Show()))) {
        return;
    }
#endif

    LOG_ERROR("No more backends to try");
}

auto Run(std::span<char*> arguments) -> int {
    auto logger = std::make_unique<Logger>();
    g_logger = logger.get();
    auto command_line = std::make_unique<CommandLine>();
    g_command_line = command_line.get();
    if (const auto exit_code = g_command_line->Parse(arguments)) {
        return *exit_code;
    }
    Platform::Init();

    if (!g_command_line->Multi() && !EnsureSingleInstance(g_command_line->Replace())) {
        std::puts("Looks like FrameXR is already running.");
        std::puts("Use --replace and I will terminate it for you.");
        return 0;
    }

    InitLogging();

    LOG_INFO("Welcome to {} version {}!", FRAMEXR_PKG_NAME, BuildVersion());
    LOG_INFO("It is {}.", LocalTime());

#if FRAMEXR_FEATURE_OPENVR
    if (g_command_line->Uninstall()) {
        OpenVrBackend::Uninstall();
        return 0;
    }
#endif

    Platform::OnTerminate(OnTerminate);
    AutoRun();
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
