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

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "Capture.hpp"
#include "Error.hpp"
#include "FrameChannel.hpp"

struct DxgiOutput {
    std::string name;
    std::wstring device;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    FrameTransform transform = FrameTransform::Normal;
};

class DxgiCapture final : public Capture {
  public:
    static auto Outputs() -> Result<std::vector<DxgiOutput>>;

    DxgiCapture(std::string name, std::wstring device);
    ~DxgiCapture() override;
    DxgiCapture(const DxgiCapture&) = delete;
    auto operator=(const DxgiCapture&) -> DxgiCapture& = delete;

    auto Start(std::span<const DrmFormat> dmabuf_formats, FrameProcessor processor) -> void override;
    [[nodiscard]] auto IsStarted() const -> bool override { return channel_ != nullptr; }
    [[nodiscard]] auto SupportsDmabuf() const -> bool override { return false; }
    auto Receive() -> std::unique_ptr<ProcessedFrame> override;
    auto Pause() -> void override;
    auto Resume() -> void override;
    auto RequestNewFrame() -> void override {}

  private:
    auto Run() -> void;

    std::string name_;
    std::wstring device_;
    FrameProcessor processor_;
    std::shared_ptr<FrameChannel<std::unique_ptr<ProcessedFrame>>> channel_;
    std::atomic<bool> running_{true};
    std::atomic<bool> paused_{false};
    std::atomic<bool> refresh_{true};
    std::thread thread_;
};
