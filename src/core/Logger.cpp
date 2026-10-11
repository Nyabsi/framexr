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

#include "Logger.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ranges>

#include "Platform.hpp"

auto Logger::Instance() -> Logger& {
    static Logger logger;
    return logger;
}

Logger::Logger() : colored_(Platform::IsTerminal(stderr)) {
    if (const char* spec = std::getenv("FRAMEXR_LOG")) {
        ParseFilters(spec);
    }
}

auto Logger::SetOutputFile(FILE* file) -> void {
    std::scoped_lock lock(mutex_);
    if (file_ != nullptr) {
        std::fclose(file_);
    }
    file_ = file;
}

auto Logger::IsEnabled(LogLevel level, std::string_view source_file) const -> bool {
    return level <= LevelFor(ModuleOf(source_file));
}

auto Logger::Write(LogLevel level, std::string_view source_file, std::string_view message) -> void {
    const auto module = ModuleOf(source_file);
    const auto timestamp = CurrentTimestamp();
    const auto plain = std::format("{} {} {}: {}\n", timestamp, LevelName(level), module, message);

    std::scoped_lock lock(mutex_);
    if (colored_) {
        const auto colored = std::format(
            "\x1b[2m{}\x1b[0m {}{}\x1b[0m \x1b[1m{}\x1b[0m: {}\n",
            timestamp,
            LevelColor(level),
            LevelName(level),
            module,
            message
        );
        std::fputs(colored.c_str(), stderr);
    } else {
        std::fputs(plain.c_str(), stderr);
    }
    if (file_ != nullptr) {
        std::fputs(plain.c_str(), file_);
        std::fflush(file_);
    }
}

auto Logger::ParseFilters(std::string_view spec) -> void {
    for (const auto token : std::views::split(spec, ',')) {
        const std::string_view entry(token.begin(), token.end());
        const auto separator = entry.find('=');
        if (separator == std::string_view::npos) {
            if (const auto level = ParseLevel(entry)) {
                default_level_ = *level;
            }
        } else if (const auto level = ParseLevel(entry.substr(separator + 1))) {
            filters_.push_back({std::string(entry.substr(0, separator)), *level});
        }
    }
}

auto Logger::LevelFor(std::string_view module) const -> LogLevel {
    const LogModuleFilter* best = nullptr;
    for (const auto& filter : filters_) {
        const bool matches = module.starts_with(filter.module)
                          && (module.size() == filter.module.size() || module[filter.module.size()] == '/');
        if (matches && (best == nullptr || filter.module.size() > best->module.size())) {
            best = &filter;
        }
    }
    return best != nullptr ? best->level : default_level_;
}

auto Logger::ModuleOf(std::string_view source_file) -> std::string_view {
    if (const auto pos = source_file.rfind("src/"); pos != std::string_view::npos) {
        source_file.remove_prefix(pos + 4);
    }
    if (const auto dot = source_file.rfind('.'); dot != std::string_view::npos) {
        source_file = source_file.substr(0, dot);
    }
    return source_file;
}

auto Logger::ParseLevel(std::string_view text) -> std::optional<LogLevel> {
    auto lower = std::string(text);
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
    if (lower == "error") {
        return LogLevel::Error;
    }
    if (lower == "warn") {
        return LogLevel::Warn;
    }
    if (lower == "info") {
        return LogLevel::Info;
    }
    if (lower == "debug") {
        return LogLevel::Debug;
    }
    if (lower == "trace") {
        return LogLevel::Trace;
    }
    return std::nullopt;
}

auto Logger::LevelName(LogLevel level) -> std::string_view {
    switch (level) {
    case LogLevel::Error: return "ERROR";
    case LogLevel::Warn: return "WARN ";
    case LogLevel::Info: return "INFO ";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Trace: return "TRACE";
    }
    return "";
}

auto Logger::LevelColor(LogLevel level) -> std::string_view {
    switch (level) {
    case LogLevel::Error: return "\x1b[31m";
    case LogLevel::Warn: return "\x1b[33m";
    case LogLevel::Info: return "\x1b[32m";
    case LogLevel::Debug: return "\x1b[34m";
    case LogLevel::Trace: return "\x1b[35m";
    }
    return "";
}

auto Logger::CurrentTimestamp() -> std::string {
    const auto now = std::chrono::floor<std::chrono::microseconds>(std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%dT%H:%M:%S}Z", now);
}
