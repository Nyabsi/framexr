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

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Capture.hpp"
#include "Error.hpp"
#include "FrameChannel.hpp"
#include "Xshm.hpp"

class XshmCapture final : public Capture {
  public:
    explicit XshmCapture(std::shared_ptr<XshmScreen> screen);
    ~XshmCapture() override;

    auto Start(std::span<const DrmFormat> dmabuf_formats, FrameProcessor processor) -> void override;
    [[nodiscard]] auto IsStarted() const -> bool override { return m_channel != nullptr; }
    [[nodiscard]] auto SupportsDmabuf() const -> bool override { return false; }
    auto Receive() -> std::unique_ptr<ProcessedFrame> override;
    auto Pause() -> void override {}
    auto Resume() -> void override;
    auto RequestNewFrame() -> void override;

    [[nodiscard]] auto Screen() const -> const std::shared_ptr<XshmScreen>& { return m_screen; }

    XshmCapture(const XshmCapture&) = delete;
    auto operator=(const XshmCapture&) -> XshmCapture& = delete;

  private:
    auto Run() -> void;
    auto WaitForRequest() -> bool;

    std::shared_ptr<XshmScreen> m_screen;
    FrameProcessor m_processor;
    std::shared_ptr<FrameChannel<std::unique_ptr<ProcessedFrame>>> m_channel;
    std::thread m_thread;
    std::mutex m_request_mutex;
    std::condition_variable m_request_signal;
    uint32_t m_pending_requests = 0;
    bool m_stopping = false;
};
