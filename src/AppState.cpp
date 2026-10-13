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

#include "AppState.hpp"

#include "AudioPlayback.hpp"
#include "Config.hpp"
#include "ConfigStore.hpp"
#include "Devices.hpp"
#include "FontCache.hpp"
#include "HidProvider.hpp"
#include "Interaction.hpp"
#include "KeyboardLayout.hpp"
#include "Logger.hpp"
#include "Osc.hpp"
#include "TaskQueue.hpp"
#include "FormTheme.hpp"
#include "framexr_config.h"

AppState::~AppState() {
    m_osc.reset();
    g_osc = nullptr;
    m_hid.reset();
    g_hid = nullptr;
    m_fonts.reset();
    g_fonts = nullptr;
    m_audio.reset();
    g_audio = nullptr;
    m_input.reset();
    g_interaction = nullptr;
    m_devices.reset();
    g_devices = nullptr;
    m_tasks.reset();
    g_tasks = nullptr;
    m_config.reset();
    g_config = nullptr;
    m_themes.reset();
    g_themes = nullptr;
}

auto AppState::Create() -> Result<std::unique_ptr<AppState>> {
    std::unique_ptr<AppState> app(new AppState());
    app->m_themes = std::make_unique<FormTheme>();
    g_themes = app->m_themes.get();

    const auto root = ConfigStore::EnsureDirectories();
    LOG_INFO("Config root path: \"{}\"", root.string());
    app->m_config = std::make_unique<Config>(ConfigStore::Load());
    g_config = app->m_config.get();
    g_themes->SetActive(g_themes->Find(g_config->theme));

    app->m_tasks = std::make_unique<TaskQueue>();
    g_tasks = app->m_tasks.get();
    app->m_devices = std::make_unique<Devices>();
    g_devices = app->m_devices.get();
    app->m_input = std::make_unique<Interaction>();
    g_interaction = app->m_input.get();

#if FRAMEXR_FEATURE_OSC
    if (auto osc = Osc::Create(g_config->osc_out_port)) {
        app->m_osc = std::move(*osc);
        g_osc = app->m_osc.get();
    }
#endif

    app->m_audio = std::make_unique<AudioPlayback>(g_config->notification_sound);
    g_audio = app->m_audio.get();
    ASSIGN_OR_RETURN(app->m_fonts, FontCache::Create(g_config->primary_font));
    app->m_fonts->SetFamily(g_themes->Active().font);
    g_fonts = app->m_fonts.get();
    app->m_hid = HidProvider::Create();
    g_hid = app->m_hid.get();
    return app;
}
