// FrameXR
//
// This is a port of wlx-capture from Rust to C++.
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
// Copyright (C) 2023-2025 galister and wlx-capture contributors
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
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <pipewire/pipewire.h>

#include "Capture.hpp"
#include "FrameChannel.hpp"

enum class PipewireCommand : uint8_t {
    Pause,
    Resume,
    Stop,
};

class PipewireCapture final : public Capture {
  public:
    PipewireCapture(std::string name, uint32_t node_id);
    ~PipewireCapture() override;

    auto Start(std::span<const DrmFormat> dmabuf_formats, FrameProcessor processor) -> void override;
    [[nodiscard]] auto IsStarted() const -> bool override { return m_channel != nullptr; }
    [[nodiscard]] auto SupportsDmabuf() const -> bool override { return true; }
    auto Receive() -> std::unique_ptr<ProcessedFrame> override;
    auto Pause() -> void override;
    auto Resume() -> void override;
    auto RequestNewFrame() -> void override {}

    PipewireCapture(const PipewireCapture&) = delete;
    auto operator=(const PipewireCapture&) -> PipewireCapture& = delete;

  private:
    auto Run() -> void;
    auto Connect() -> void;
    auto ScheduleRetry() -> void;
    auto SendCommand(PipewireCommand command) -> void;
    auto ExecuteCommands() -> void;
    auto UpdateStreamParams() -> void;
    auto HandleBuffer(const spa_buffer& buffer) -> void;

    static auto OnStateChanged(void* data, pw_stream_state old_state, pw_stream_state state, const char* error) -> void;
    static auto OnParamChanged(void* data, uint32_t id, const spa_pod* param) -> void;
    static auto OnProcess(void* data) -> void;
    static auto OnCommand(void* data, uint64_t count) -> void;
    static auto OnRetry(void* data, uint64_t expirations) -> void;

    std::string m_name;
    uint32_t m_node_id;
    std::vector<DrmFormat> m_dmabuf_formats;
    FrameProcessor m_processor;
    std::shared_ptr<FrameChannel<std::unique_ptr<ProcessedFrame>>> m_channel;
    std::thread m_thread;

    std::mutex m_command_mutex;
    std::deque<PipewireCommand> m_commands;
    pw_main_loop* m_loop = nullptr;
    spa_source* m_command_event = nullptr;

    pw_stream* m_stream = nullptr;
    spa_hook m_stream_listener{};
    spa_source* m_retry_timer = nullptr;
    uint32_t m_failures = 0;
    bool m_active = true;
    FrameFormat m_format;
};
