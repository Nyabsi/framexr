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

#include "FakeCapture.hpp"

#include <utility>

FakeCapture::FakeCapture(uint32_t width, uint32_t height, std::vector<std::byte> bgra)
    : width_(width),
      height_(height),
      bgra_(std::move(bgra)) {
}

auto FakeCapture::Start(std::span<const DrmFormat>, FrameProcessor processor) -> void {
    processor_ = std::move(processor);
}

auto FakeCapture::IsStarted() const -> bool {
    return static_cast<bool>(processor_);
}

auto FakeCapture::SupportsDmabuf() const -> bool {
    return false;
}

auto FakeCapture::Receive() -> std::unique_ptr<ProcessedFrame> {
    return std::move(pending_);
}

auto FakeCapture::Pause() -> void {
}

auto FakeCapture::Resume() -> void {
}

auto FakeCapture::RequestNewFrame() -> void {
    if (!processor_ || pending_) {
        return;
    }
    pending_ = processor_(
        MemPtrFrame{
            .format = {.width = width_, .height = height_, .fourcc = kDrmFormatXrgb8888, .transform = FrameTransform::Normal},
            .data = bgra_.data(),
            .size = bgra_.size(),
        }
    );
}
