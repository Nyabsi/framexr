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
#include "Hid.hpp"
#include "State.hpp"

std::unique_ptr<AppState> TestApp::app_;
std::filesystem::path TestApp::output_dir_;

auto TestApp::Get() -> AppState& {
    return *app_;
}

auto TestApp::OutputDir() -> const std::filesystem::path& {
    return output_dir_;
}

auto TestApp::SetUp() -> void {
    const char* output = std::getenv("FRAMEXR_TEST_OUTPUT");
    output_dir_ = output != nullptr ? std::filesystem::path(output) : std::filesystem::current_path() / "test-output";
    std::filesystem::create_directories(output_dir_);

    HidProvider::use_uinput.store(false);
    auto device = Device::Create({});
    ASSERT_TRUE(device.has_value()) << device.error().Message();
    auto app = AppState::Create(*device);
    ASSERT_TRUE(app.has_value()) << app.error().Message();
    app_ = std::move(*app);
}

auto TestApp::TearDown() -> void {
    app_.reset();
}
