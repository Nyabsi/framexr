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

#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

#include "InputMethod.hpp"
#include "Process.hpp"

class DesktopInputMethod final : public InputMethod
{
  public:
    DesktopInputMethod();
    ~DesktopInputMethod() override;
    DesktopInputMethod(const DesktopInputMethod&) = delete;
    auto operator=(const DesktopInputMethod&) -> DesktopInputMethod& = delete;

    auto Select(ImeScript script) -> void override;

  private:
    auto Work(const std::stop_token& stop) -> void;
    auto Apply(ImeScript script) -> void;
    auto Start() -> std::optional<std::pair<size_t, std::string>>;
    auto Launch() -> bool;

    ImeScript m_active = ImeScript::None;
    size_t m_framework = 0;
    std::string m_previous;
    bool m_launch_tried = false;
    std::optional<Process> m_launched;
    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::optional<ImeScript> m_pending;
    std::jthread m_worker;
};
