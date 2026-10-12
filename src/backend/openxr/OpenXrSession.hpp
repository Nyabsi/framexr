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
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <openxr/openxr.h>

#include "Error.hpp"
#include "OpenXrInstance.hpp"

class OpenXrSession final {
  public:
    static auto Create(const OpenXrInstance& instance) -> Result<std::unique_ptr<OpenXrSession>>;

    ~OpenXrSession();
    OpenXrSession(const OpenXrSession&) = delete;
    auto operator=(const OpenXrSession&) -> OpenXrSession& = delete;

    [[nodiscard]] auto Handle() const -> XrSession { return m_session; }
    [[nodiscard]] auto Stage() const -> XrSpace { return m_stage; }
    [[nodiscard]] auto View() const -> XrSpace { return m_view; }

  private:
    explicit OpenXrSession(XrSession session);

    static auto CreateReferenceSpace(const OpenXrInstance& instance, XrSession session, XrReferenceSpaceType type)
        -> Result<XrSpace>;

    XrSession m_session;
    XrSpace m_stage = XR_NULL_HANDLE;
    XrSpace m_view = XR_NULL_HANDLE;
};
