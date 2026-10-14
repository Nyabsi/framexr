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

#include "AudioPlayback.hpp"
#include "CommandLine.hpp"
#include "Config.hpp"
#include "ConfigStore.hpp"
#include "Devices.hpp"
#include "FormTheme.hpp"
#include "HidProvider.hpp"
#include "Interaction.hpp"
#include "Logger.hpp"
#include "Osc.hpp"
#include "Platform.hpp"
#include "Runtime.hpp"
#include "TaskQueue.hpp"
#include "framexr_config.h"
#include "version.h"

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
        std::fputs(std::format("Failed to open log file (path: {}): {}\n", path.string(), file.error().Message()).c_str(), stdout);
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

auto Run(std::span<char*> arguments) -> int {
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

    if (g_command_line->Uninstall()) {
        Runtime::Uninstall();
        return 0;
    }

    auto themes = std::make_unique<FormTheme>();
    g_themes = themes.get();
    LOG_INFO("Config root path: \"{}\"", ConfigStore::EnsureDirectories().string());
    auto config = std::make_unique<Config>(ConfigStore::Load());
    g_config = config.get();
    g_themes->SetActive(g_themes->Find(g_config->theme));
    auto tasks = std::make_unique<TaskQueue>();
    g_tasks = tasks.get();
    auto devices = std::make_unique<Devices>();
    g_devices = devices.get();
    auto interaction = std::make_unique<Interaction>();
    g_interaction = interaction.get();
    auto audio = std::make_unique<AudioPlayback>(g_config->notification_sound);
    g_audio = audio.get();
    auto hid = HidProvider::Create();
    g_hid = hid.get();
#if FRAMEXR_FEATURE_OSC
    auto osc = Osc::Create(g_config->osc_out_port).value_or(nullptr);
    g_osc = osc.get();
#endif

    Platform::OnTerminate(OnTerminate);
    Runtime::Run(running);
    return 0;
}

} // namespace

auto main(int argc, char** argv) -> int {
    auto logger = std::make_unique<Logger>();
    g_logger = logger.get();
    try {
        return Run(std::span(argv, static_cast<size_t>(argc)));
    } catch (const std::exception& exception) {
        FatalError(exception.what());
    }
}
