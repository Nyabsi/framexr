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
#include <cstdio>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

enum class LogLevel : std::uint8_t {
    Error,
    Warn,
    Info,
    Debug,
    Trace,
};

struct LogModuleFilter {
    std::string module;
    LogLevel level;
};

class Logger final {
  public:
    Logger();

    auto SetOutputFile(FILE* file) -> void;
    [[nodiscard]] auto IsEnabled(LogLevel level, std::string_view source_file) const -> bool;
    auto Write(LogLevel level, std::string_view source_file, std::string_view message) -> void;

    Logger(const Logger&) = delete;
    auto operator=(const Logger&) -> Logger& = delete;

  private:
    auto ParseFilters(std::string_view spec) -> void;
    [[nodiscard]] auto LevelFor(std::string_view module) const -> LogLevel;

    static auto ModuleOf(std::string_view source_file) -> std::string_view;
    static auto ParseLevel(std::string_view text) -> std::optional<LogLevel>;
    static auto LevelName(LogLevel level) -> std::string_view;
    static auto LevelColor(LogLevel level) -> std::string_view;
    static auto CurrentTimestamp() -> std::string;

    std::mutex m_mutex;
    std::vector<LogModuleFilter> m_filters;
    LogLevel m_default_level = LogLevel::Info;
    FILE* m_file = nullptr;
    bool m_colored;
};

extern Logger* g_logger;

#define LOG_IMPL(level, ...)                                               \
    do {                                                                   \
        if (g_logger != nullptr && g_logger->IsEnabled(level, __FILE__)) { \
            g_logger->Write(level, __FILE__, std::format(__VA_ARGS__));    \
        }                                                                  \
    } while (false)

#define LOG_ERROR(...) LOG_IMPL(LogLevel::Error, __VA_ARGS__)
#define LOG_WARN(...) LOG_IMPL(LogLevel::Warn, __VA_ARGS__)
#define LOG_INFO(...) LOG_IMPL(LogLevel::Info, __VA_ARGS__)
#define LOG_DEBUG(...) LOG_IMPL(LogLevel::Debug, __VA_ARGS__)
#define LOG_TRACE(...) LOG_IMPL(LogLevel::Trace, __VA_ARGS__)
