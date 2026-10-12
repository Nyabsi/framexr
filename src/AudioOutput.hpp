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

#pragma once

#include <cstddef>
#include <list>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

struct ma_engine;
struct ma_decoder;
struct ma_sound;

struct PlayingSound {
    std::unique_ptr<ma_decoder> decoder;
    std::unique_ptr<ma_sound> sound;
};

class AudioOutput final {
  public:
    explicit AudioOutput(std::string_view notification_sound);
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    auto operator=(const AudioOutput&) -> AudioOutput& = delete;

    auto Play(std::span<const std::byte> wav) -> void;
    auto PlayNotification() -> void;

  private:
    static auto LoadBytes(std::string_view path, std::span<const std::byte> fallback) -> std::vector<std::byte>;

    auto Engine() -> ma_engine*;
    auto ReapFinished() -> void;

    std::unique_ptr<ma_engine> m_engine;
    std::list<PlayingSound> m_playing;
    std::vector<std::byte> m_notification_sound;
    bool m_first_try = true;
};

extern AudioOutput* g_audio;
