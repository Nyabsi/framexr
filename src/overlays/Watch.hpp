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

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Error.hpp"
#include "Modular.hpp"
#include "Overlay.hpp"

class AppState;

constexpr std::string_view kWatchName = "watch";

class Watch {
  public:
    static auto Create(AppState& app) -> Result<std::unique_ptr<Overlay>>;
    static auto RebuildCanvas(AppState& app, Overlay& watch) -> Result<>;
    static auto Apply(AppState& app, Overlay& watch, const ModularUiConfig& config) -> Result<>;
    static constexpr std::string_view kDefaultProfile = "default";

    static auto LoadConfig(std::string_view profile) -> ModularUiConfig;
    static auto UserFile(std::string_view profile) -> std::filesystem::path;
    static auto Profiles() -> std::vector<std::string>;
    static auto IsBuiltIn(std::string_view profile) -> bool;
    static auto Fade(AppState& app, Overlay& watch) -> void;
};
