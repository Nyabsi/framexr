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

#include "ActionRunner.hpp"
#include "ButtonAction.hpp"
#include "Control.hpp"
#include "Osc.hpp"
#include "Process.hpp"
#include "Task.hpp"
#include "VirtualKey.hpp"

class ModularButton final : public Control {
  public:
    explicit ModularButton(ButtonData data);

    auto OnPress(PointerMode mode) -> void override;
    auto OnRelease() -> void override;
    auto OnScroll(float delta_y, float delta_x) -> void override;
    auto HighlightColor() -> std::optional<Eigen::Vector4f> override;

  private:
    auto Run(const std::optional<std::vector<ButtonAction>>& actions) -> void;

    ButtonData m_data;
    ActionRunner m_runner;
    std::chrono::steady_clock::time_point m_last_down;
    PointerMode m_last_mode = PointerMode::Left;
};
