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

#include <filesystem>
#include <memory>

#include <gtest/gtest.h>

class AudioPlayback;
class Device;
class Devices;
class FontCache;
class FormTheme;
class HidProvider;
class Interaction;
class Logger;
class TaskQueue;
struct Config;

class TestApp final : public testing::Environment
{
  public:
    static auto OutputDir() -> const std::filesystem::path&;

    auto SetUp() -> void override;
    auto TearDown() -> void override;

  private:
    static std::unique_ptr<Logger> m_logger;
    static std::unique_ptr<Device> m_device;
    static std::unique_ptr<FormTheme> m_themes;
    static std::unique_ptr<Config> m_config;
    static std::unique_ptr<TaskQueue> m_tasks;
    static std::unique_ptr<Devices> m_devices;
    static std::unique_ptr<Interaction> m_interaction;
    static std::unique_ptr<AudioPlayback> m_audio;
    static std::unique_ptr<FontCache> m_fonts;
    static std::unique_ptr<HidProvider> m_hid;
    static std::filesystem::path m_output_dir;
};
