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

#include "TestApp.hpp"

#include <cstdlib>

#include "AudioPlayback.hpp"
#include "Config.hpp"
#include "ConfigStore.hpp"
#include "Device.hpp"
#include "Devices.hpp"
#include "FontCache.hpp"
#include "FormTheme.hpp"
#include "HidProvider.hpp"
#include "Interaction.hpp"
#include "Logger.hpp"
#include "TaskQueue.hpp"

std::unique_ptr<Logger> TestApp::m_logger;
std::unique_ptr<Device> TestApp::m_device;
std::unique_ptr<FormTheme> TestApp::m_themes;
std::unique_ptr<Config> TestApp::m_config;
std::unique_ptr<TaskQueue> TestApp::m_tasks;
std::unique_ptr<Devices> TestApp::m_devices;
std::unique_ptr<Interaction> TestApp::m_interaction;
std::unique_ptr<AudioPlayback> TestApp::m_audio;
std::unique_ptr<FontCache> TestApp::m_fonts;
std::unique_ptr<HidProvider> TestApp::m_hid;
std::filesystem::path TestApp::m_output_dir;

auto TestApp::OutputDir() -> const std::filesystem::path&
{
    return m_output_dir;
}

auto TestApp::SetUp() -> void
{
    const char* output = std::getenv("FRAMEXR_TEST_OUTPUT");
    m_output_dir = output != nullptr ? std::filesystem::path(output) : std::filesystem::current_path() / "test-output";
    std::filesystem::create_directories(m_output_dir);

    m_logger = std::make_unique<Logger>();
    g_logger = m_logger.get();
    HidProvider::SetUseNative(false);
    auto device = Device::Create({});
    ASSERT_TRUE(device.has_value()) << device.error().Message();
    m_device = std::move(*device);
    m_themes = std::make_unique<FormTheme>();
    g_themes = m_themes.get();
    ConfigStore::EnsureDirectories();
    m_config = std::make_unique<Config>(ConfigStore::Load());
    g_config = m_config.get();
    g_themes->SetActive(g_themes->Find(g_config->theme));
    m_tasks = std::make_unique<TaskQueue>();
    g_tasks = m_tasks.get();
    m_devices = std::make_unique<Devices>();
    g_devices = m_devices.get();
    m_interaction = std::make_unique<Interaction>();
    g_interaction = m_interaction.get();
    m_audio = std::make_unique<AudioPlayback>(g_config->notification_sound);
    g_audio = m_audio.get();
    auto fonts = FontCache::Create(g_config->primary_font);
    ASSERT_TRUE(fonts.has_value()) << fonts.error().Message();
    m_fonts = std::move(*fonts);
    m_fonts->SetFamily(g_themes->Active().font);
    g_fonts = m_fonts.get();
    m_hid = HidProvider::Create();
    g_hid = m_hid.get();
}

auto TestApp::TearDown() -> void
{
    m_hid.reset();
    m_fonts.reset();
    g_fonts = nullptr;
    m_audio.reset();
    m_interaction.reset();
    m_devices.reset();
    m_tasks.reset();
    m_config.reset();
    m_themes.reset();
    m_device.reset();
    m_logger.reset();
    g_logger = nullptr;
}
