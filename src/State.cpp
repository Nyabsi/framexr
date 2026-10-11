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

#include "State.hpp"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <miniaudio.h>
#pragma GCC diagnostic pop

#include <format>

#include "Font.hpp"
#include "Hid.hpp"
#include "Logger.hpp"
#include "Osc.hpp"
#include "Theme.hpp"
#include "framexr_config.h"

namespace {

constexpr unsigned char kToastSound[] = {
#embed "557297.wav"
};

} // namespace

AudioOutput::AudioOutput() = default;

AudioOutput::~AudioOutput() {
    for (auto& playing : playing_) {
        ma_sound_uninit(playing.sound.get());
        ma_decoder_uninit(playing.decoder.get());
    }
    playing_.clear();
    if (engine_) {
        ma_engine_uninit(engine_.get());
    }
}

auto AudioOutput::Engine() -> ma_engine* {
    if (!engine_ && first_try_) {
        first_try_ = false;
        engine_ = std::make_unique<ma_engine>();
        if (ma_engine_init(nullptr, engine_.get()) != MA_SUCCESS) {
            LOG_ERROR("Failed to open audio stream. Audio will not work.");
            engine_.reset();
        }
    }
    return engine_.get();
}

auto AudioOutput::ReapFinished() -> void {
    std::erase_if(playing_, [](PlayingSound& playing) -> bool {
        if (ma_sound_at_end(playing.sound.get()) == MA_FALSE) {
            return false;
        }
        ma_sound_uninit(playing.sound.get());
        ma_decoder_uninit(playing.decoder.get());
        return true;
    });
}

auto AudioOutput::Play(std::span<const std::byte> wav) -> void {
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
    playing_.push_back(std::move(playing));
}

AppState::AppState() = default;

AppState::~AppState() = default;

auto AppState::Create(std::shared_ptr<Device> device) -> Result<std::unique_ptr<AppState>> {
    std::unique_ptr<AppState> app(new AppState());
    app->device = std::move(device);

    const auto root = Config::EnsureDirectories();
    LOG_INFO("Config root path: \"{}\"", root.string());
    app->config = Config::Load();
    Theme::SetActive(Theme::Find(app->config.theme));

    app->toast_topics = {
        {ToastTopic::System, ToastDisplay::Center},
        {ToastTopic::DesktopNotification, ToastDisplay::Center},
        {ToastTopic::XsNotification, ToastDisplay::Center},
    };
    for (const auto& [topic, display] : app->config.notification_topics) {
        app->toast_topics[topic] = display;
    }

#if FRAMEXR_FEATURE_OSC
    if (auto osc = OscSender::Create(app->config.osc_out_port)) {
        app->osc = std::move(*osc);
    }
#endif

    app->toast_sound = LoadBytes(app->config.notification_sound, std::as_bytes(std::span(kToastSound)));
    ASSIGN_OR_RETURN(app->fonts, FontCache::Create(app->config.primary_font));
    app->hid = HidProvider::Create();
    return app;
}

auto AppState::LoadBytes(std::string_view path, std::span<const std::byte> fallback) -> std::vector<std::byte> {
    const auto fallback_bytes = [&] -> std::vector<std::byte> { return {fallback.begin(), fallback.end()}; };
    if (path.empty()) {
        return fallback_bytes();
    }
    const auto contents = Config::ReadFile(Config::Root() / path);
    if (!contents) {
        LOG_WARN("Could not open file at: {}", path);
        return fallback_bytes();
    }
    const auto bytes = std::as_bytes(std::span(*contents));
    return {bytes.begin(), bytes.end()};
}
