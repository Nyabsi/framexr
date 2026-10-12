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

#include "Hid.hpp"

#include <cstdlib>

#include "Logger.hpp"
#include "NullHidProvider.hpp"
#include "UInputProvider.hpp"

auto HidProvider::Create() -> std::unique_ptr<HidProvider> {
    if (!UseNative()) {
        LOG_INFO("Uinput disabled by user.");
        return std::make_unique<NullHidProvider>();
    }
    if (auto uinput = UInputProvider::Create()) {
        LOG_INFO("Initialized uinput.");
        return uinput;
    }
    LOG_ERROR("Could not create uinput provider. Keyboard/Mouse input will not work!");
    LOG_ERROR("To check if you're in input group, run: id -nG");
    if (const char* user = std::getenv("USER")) {
        LOG_ERROR("To add yourself to the input group, run: sudo usermod -aG input {}", user);
        LOG_ERROR("After adding yourself to the input group, you will need to reboot.");
    }
    return std::make_unique<NullHidProvider>();
}
