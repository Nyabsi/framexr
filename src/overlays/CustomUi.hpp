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
#include <optional>
#include <string>
#include <utility>

#include "Error.hpp"
#include "Modular.hpp"
#include "Overlay.hpp"

class AppState;

class CustomUi {
  public:
    static auto Load(const std::string& name) -> Result<ModularUiConfig>;
    static auto Create(AppState& app, std::string name) -> std::optional<std::pair<OverlayState, std::unique_ptr<OverlayBackend>>>;
};
