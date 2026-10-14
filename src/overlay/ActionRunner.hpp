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

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <Eigen/Core>
#include <nlohmann/json.hpp>

#include "document/Actions.hpp"
#include "Process.hpp"
#include "TaskQueue.hpp"
#include "VirtualKeys.hpp"

class ActionRunner final
{
  public:
    auto Run(const std::vector<ButtonAction>& actions) -> void;
    auto Run(const ButtonAction& action) -> void;

    static auto PlayThump() -> void;
    static auto PressShortcut(const std::vector<VirtualKey>& keys) -> void;
    static auto TypeText(std::string_view text) -> void;

  private:
    auto RunExec(const ExecButtonAction& action) -> void;

    static auto RunSystem(SystemAction action) -> void;
    static auto RunWatch(const WatchAction& action) -> void;
    static auto RunOverlay(const OverlaySelector& target, const OverlayAction& action) -> void;
    static auto RunWindow(const std::string& target, WindowAction action) -> void;
    static auto RunProfile(const std::optional<std::string>& profile) -> void;

    std::optional<Process> m_child;
};
