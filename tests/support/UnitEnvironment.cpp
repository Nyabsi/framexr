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

#include "UnitEnvironment.hpp"

#include "KeyboardProfiles.hpp"
#include "Logger.hpp"
#include "Theme.hpp"

UnitEnvironment::UnitEnvironment() = default;

UnitEnvironment::~UnitEnvironment() = default;

auto UnitEnvironment::SetUp() -> void {
    m_logger = std::make_unique<Logger>();
    g_logger = m_logger.get();
    m_themes = std::make_unique<Theme>();
    g_themes = m_themes.get();
    m_keyboard_profiles = std::make_unique<KeyboardProfiles>();
    g_keyboard_profiles = m_keyboard_profiles.get();
}

auto UnitEnvironment::TearDown() -> void {
    m_keyboard_profiles.reset();
    g_keyboard_profiles = nullptr;
    m_themes.reset();
    g_themes = nullptr;
    m_logger.reset();
    g_logger = nullptr;
}
