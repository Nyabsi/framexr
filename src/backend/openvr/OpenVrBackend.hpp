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

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

#include "Backend.hpp"
#include "Device.hpp"
#include "Error.hpp"
#include "Notifications.hpp"
#include "OpenVrInput.hpp"
#include "OpenVrLines.hpp"
#include "OpenVrOverlay.hpp"
#include "OpenVrPlayspace.hpp"
#include "OverlayContainer.hpp"
#include "State.hpp"
#include "Task.hpp"

class OpenVrBackend {
  public:
    static auto Run(std::atomic<bool>& running, bool show_by_default) -> BackendResult;
    static auto Uninstall() -> void;
};
