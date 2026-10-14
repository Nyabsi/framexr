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

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "Capture.hpp"

class FakeCapture final : public Capture
{
  public:
    FakeCapture(uint32_t width, uint32_t height, std::vector<std::byte> bgra);

    auto Start(std::span<const DrmFormat> dmabuf_formats, FrameProcessor processor) -> void override;
    [[nodiscard]] auto IsStarted() const -> bool override;
    [[nodiscard]] auto SupportsDmabuf() const -> bool override;
    auto Receive() -> std::unique_ptr<ProcessedFrame> override;
    auto Pause() -> void override;
    auto Resume() -> void override;
    auto RequestNewFrame() -> void override;

  private:
    uint32_t m_width;
    uint32_t m_height;
    std::vector<std::byte> m_bgra;
    FrameProcessor m_processor;
    std::unique_ptr<ProcessedFrame> m_pending;
};
