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

#include "Overlay.hpp"

#include "Config.hpp"
#include "Input.hpp"
#include "SplitOverlayBackend.hpp"

Overlay::Overlay(OverlayState overlay_state, std::unique_ptr<OverlayBackend> overlay_backend)
    : m_state(std::move(overlay_state)),
      m_backend(overlay_backend ? std::move(overlay_backend) : std::make_unique<SplitOverlayBackend>()) {
}

auto Overlay::Init() -> Result<> {
    if (!m_state.Curvature()) {
        for (const auto& [name, value] : g_config->curve_values) {
            if (name == m_state.Name()) {
                m_state.SetCurvature(value);
            }
        }
    }

    if (m_state.AttachedTo() == RelativeTo::None) {
        bool hard_reset = true;
        for (const auto& [name, transform] : g_config->transform_values) {
            if (name == m_state.Name()) {
                m_state.SetSavedTransform(transform);
                hard_reset = false;
            }
        }
        m_state.Reset(hard_reset);
    }
    return m_backend->Init();
}
