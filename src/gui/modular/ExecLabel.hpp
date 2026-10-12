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
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <Eigen/Core>
#include <nlohmann/json.hpp>

#include "Control.hpp"
#include "Process.hpp"

class ExecLabel final : public Control {
  public:
    ExecLabel(std::vector<std::string> command, float interval);

    auto Update() -> void override;

  private:
    std::vector<std::string> m_command;
    float m_interval;
    std::chrono::steady_clock::time_point m_last_exec;
    std::optional<Process> m_child;
};
