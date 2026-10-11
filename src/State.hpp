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
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <Eigen/Geometry>

#include "Config.hpp"
#include "Device.hpp"
#include "Input.hpp"
#include "Overlay.hpp"
#include "Task.hpp"
#include "Toast.hpp"

struct ma_engine;
struct ma_decoder;
struct ma_sound;

struct PlayingSound {
    std::unique_ptr<ma_decoder> decoder;
    std::unique_ptr<ma_sound> sound;
};

class AudioOutput final {
  public:
    AudioOutput();
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    auto operator=(const AudioOutput&) -> AudioOutput& = delete;

    auto Play(std::span<const std::byte> wav) -> void;

  private:
    auto Engine() -> ma_engine*;
    auto ReapFinished() -> void;

    std::unique_ptr<ma_engine> engine_;
    std::list<PlayingSound> playing_;
    bool first_try_ = true;
};

class FontCache;
class HidProvider;
class Keymap;
class OscSender;

struct ScreenMeta {
    std::string name;
    OverlayId id{};
    uint32_t native_handle = 0;
};

class AppState final {
  public:
    static auto Create(std::shared_ptr<Device> device) -> Result<std::unique_ptr<AppState>>;

    ~AppState();
    AppState(const AppState&) = delete;
    auto operator=(const AppState&) -> AppState& = delete;

    std::shared_ptr<Device> device;
    Config config;
    std::map<ToastTopic, ToastDisplay> toast_topics;
    std::unique_ptr<FontCache> fonts;
    TaskQueue tasks;
    InputState input;
    std::unique_ptr<HidProvider> hid;
    AudioOutput audio;
    std::vector<ScreenMeta> screens;
    std::shared_ptr<const Keymap> keymap;
    Eigen::Vector2f display_pixels_per_radian = Eigen::Vector2f::Zero();
    std::map<std::string, std::shared_ptr<Image>, std::less<>> sprites;
    std::vector<std::byte> toast_sound;
    std::unique_ptr<OscSender> osc;

    static auto LoadBytes(std::string_view path, std::span<const std::byte> fallback) -> std::vector<std::byte>;

  private:
    AppState();
};
