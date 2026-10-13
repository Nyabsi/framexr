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

#include "Device.hpp"
#include "HidProvider.hpp"
#include "Logger.hpp"
#include "AppState.hpp"

std::unique_ptr<Logger> TestApp::m_logger;
std::unique_ptr<Device> TestApp::m_device;
std::unique_ptr<AppState> TestApp::m_app;
std::filesystem::path TestApp::m_output_dir;

auto TestApp::OutputDir() -> const std::filesystem::path& {
    return m_output_dir;
}

auto TestApp::SetUp() -> void {
    const char* output = std::getenv("FRAMEXR_TEST_OUTPUT");
    m_output_dir = output != nullptr ? std::filesystem::path(output) : std::filesystem::current_path() / "test-output";
    std::filesystem::create_directories(m_output_dir);

    m_logger = std::make_unique<Logger>();
    g_logger = m_logger.get();
    HidProvider::SetUseNative(false);
    auto device = Device::Create({});
    ASSERT_TRUE(device.has_value()) << device.error().Message();
    m_device = std::move(*device);
    auto app = AppState::Create();
    ASSERT_TRUE(app.has_value()) << app.error().Message();
    m_app = std::move(*app);
}

auto TestApp::TearDown() -> void {
    m_app.reset();
    m_device.reset();
    m_logger.reset();
    g_logger = nullptr;
}
