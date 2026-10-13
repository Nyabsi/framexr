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

#include <memory>

#include "Error.hpp"

struct Config;
class AudioPlayback;
class Devices;
class FontCache;
class HidProvider;
class Interaction;
class Osc;
class TaskQueue;
class FormTheme;

class AppState final {
  public:
    static auto Create() -> Result<std::unique_ptr<AppState>>;

    ~AppState();
    AppState(const AppState&) = delete;
    auto operator=(const AppState&) -> AppState& = delete;

  private:
    AppState() = default;

    std::unique_ptr<FormTheme> m_themes;
    std::unique_ptr<Config> m_config;
    std::unique_ptr<TaskQueue> m_tasks;
    std::unique_ptr<Devices> m_devices;
    std::unique_ptr<Interaction> m_input;
    std::unique_ptr<AudioPlayback> m_audio;
    std::unique_ptr<FontCache> m_fonts;
    std::unique_ptr<HidProvider> m_hid;
    std::unique_ptr<Osc> m_osc;
};
