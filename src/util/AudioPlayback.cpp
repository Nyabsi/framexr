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

#include "AudioPlayback.hpp"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <miniaudio.h>
#pragma GCC diagnostic pop

#include "Config.hpp"
#include "ConfigStore.hpp"
#include "Logger.hpp"

namespace {

constexpr unsigned char kToastSound[] = {
#embed "557297.wav"
};

} // namespace

AudioPlayback* g_audio = nullptr;

AudioPlayback::AudioPlayback(std::string_view notification_sound)
    : m_notification_sound(LoadBytes(notification_sound, std::as_bytes(std::span(kToastSound)))) {
}

AudioPlayback::~AudioPlayback() {
    for (auto& playing : m_playing) {
        ma_sound_uninit(playing.sound.get());
        ma_decoder_uninit(playing.decoder.get());
    }
    m_playing.clear();
    if (m_engine) {
        ma_engine_uninit(m_engine.get());
    }
}

auto AudioPlayback::Engine() -> ma_engine* {
    if (!m_engine && m_first_try) {
        m_first_try = false;
        m_engine = std::make_unique<ma_engine>();
        if (ma_engine_init(nullptr, m_engine.get()) != MA_SUCCESS) {
            LOG_ERROR("Failed to open audio stream. Audio will not work.");
            m_engine.reset();
        }
    }
    return m_engine.get();
}

auto AudioPlayback::ReapFinished() -> void {
    std::erase_if(m_playing, [](PlayingSound& playing) -> bool {
        if (ma_sound_at_end(playing.sound.get()) == MA_FALSE) {
            return false;
        }
        ma_sound_uninit(playing.sound.get());
        ma_decoder_uninit(playing.decoder.get());
        return true;
    });
}

auto AudioPlayback::Play(std::span<const std::byte> wav) -> void {
    auto* engine = Engine();
    if (engine == nullptr) {
        return;
    }
    ReapFinished();

    PlayingSound playing{.decoder = std::make_unique<ma_decoder>(), .sound = std::make_unique<ma_sound>()};
    const auto decoder_config = ma_decoder_config_init(ma_format_f32, 0, 0);
    if (const auto result = ma_decoder_init_memory(wav.data(), wav.size(), &decoder_config, playing.decoder.get());
        result != MA_SUCCESS) {
        LOG_ERROR("Failed to play sound: {}", ma_result_description(result));
        return;
    }
    if (const auto result = ma_sound_init_from_data_source(engine, playing.decoder.get(), 0, nullptr, playing.sound.get());
        result != MA_SUCCESS) {
        ma_decoder_uninit(playing.decoder.get());
        LOG_ERROR("Failed to play sound: {}", ma_result_description(result));
        return;
    }
    ma_sound_start(playing.sound.get());
    m_playing.push_back(std::move(playing));
}

auto AudioPlayback::PlayNotification() -> void {
    Play(m_notification_sound);
}

auto AudioPlayback::LoadBytes(std::string_view path, std::span<const std::byte> fallback) -> std::vector<std::byte> {
    const auto fallback_bytes = [&] -> std::vector<std::byte> { return {fallback.begin(), fallback.end()}; };
    if (path.empty()) {
        return fallback_bytes();
    }
    const auto contents = ConfigStore::ReadFile(ConfigStore::Root() / path);
    if (!contents) {
        LOG_WARN("Could not open file at: {}", path);
        return fallback_bytes();
    }
    const auto bytes = std::as_bytes(std::span(*contents));
    return {bytes.begin(), bytes.end()};
}
