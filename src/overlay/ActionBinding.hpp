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
#include <optional>
#include <vector>

#include <Eigen/Core>

#include "ActionRunner.hpp"
#include "FormComponent.hpp"
#include "methods/LaserPointer.hpp"
#include "document/Actions.hpp"

class ActionBinding final {
  public:
    static auto Bind(FormComponent& button, const ButtonData& data) -> void;

    explicit ActionBinding(ButtonData data);

    auto Press(PointerMode mode) -> void;
    auto Release() -> void;
    auto Scroll(float delta_y) -> void;
    [[nodiscard]] auto Highlight() const -> std::optional<Eigen::Vector4f>;

  private:
    auto Run(const std::optional<std::vector<ButtonAction>>& actions) -> void;

    ButtonData m_data;
    ActionRunner m_runner;
    std::chrono::steady_clock::time_point m_last_down;
    PointerMode m_last_mode = PointerMode::Left;
};
