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
    : m_width(width),
      m_height(height),
      m_bgra(std::move(bgra))
{
}

auto FakeCapture::Start(std::span<const DrmFormat>, FrameProcessor processor) -> void
{
    m_processor = std::move(processor);
}

auto FakeCapture::IsStarted() const -> bool
{
    return static_cast<bool>(m_processor);
}

auto FakeCapture::SupportsDmabuf() const -> bool
{
    return false;
}

auto FakeCapture::Receive() -> std::unique_ptr<ProcessedFrame>
{
    return std::move(m_pending);
}

auto FakeCapture::Pause() -> void
{
}

auto FakeCapture::Resume() -> void
{
}

auto FakeCapture::RequestNewFrame() -> void
{
    if (!m_processor || m_pending)
    {
        return;
    }
    m_pending = m_processor(
        MemPtrFrame{
            .format = {.width = m_width, .height = m_height, .fourcc = kDrmFormatXrgb8888, .transform = FrameTransform::Normal},
            .data = m_bgra.data(),
            .size = m_bgra.size(),
        }
    );
}
