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

#include <functional>
#include <memory>
#include <span>

#include "Frame.hpp"
#include "ProcessedFrame.hpp"

using FrameProcessor = std::function<std::unique_ptr<ProcessedFrame>(Frame&&)>;

class Capture {
  public:
    virtual ~Capture() = default;

    virtual auto Start(std::span<const DrmFormat> dmabuf_formats, FrameProcessor processor) -> void = 0;
    [[nodiscard]] virtual auto IsStarted() const -> bool = 0;
    [[nodiscard]] virtual auto SupportsDmabuf() const -> bool = 0;
    virtual auto Receive() -> std::unique_ptr<ProcessedFrame> = 0;
    virtual auto Pause() -> void = 0;
    virtual auto Resume() -> void = 0;
    virtual auto RequestNewFrame() -> void = 0;
};
