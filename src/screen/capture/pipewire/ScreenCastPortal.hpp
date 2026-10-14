// FrameXR
//
// This is a port of wlx-capture from Rust to C++.
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
// Copyright (C) 2023-2025 galister and wlx-capture contributors
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

#include <cstdint>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Core>

#include "Error.hpp"

struct DBusConnection;
struct DBusMessageIter;

struct PipewireStream
{
    uint32_t node_id = 0;
    std::optional<Eigen::Vector2i> position;
    std::optional<Eigen::Vector2i> size;
};

struct ScreenSelection
{
    std::vector<PipewireStream> streams;
    std::optional<std::string> restore_token;
};

struct ScreenCastOptions
{
    std::optional<std::string> restore_token;
    bool embed_mouse = false;
    bool screens_only = false;
    bool persist = false;
    bool multiple = false;
};

enum class PortalCursorMode : uint8_t
{
    Hidden = 1,
    Embedded = 2,
    Metadata = 4,
};

using PortalOptionWriter = std::function<void(DBusMessageIter& dictionary)>;
using PortalResultVisitor = std::function<void(const std::string& key, DBusMessageIter& variant)>;

class ScreenCastPortal final
{
  public:
    static auto SelectScreen(ScreenCastOptions options) -> std::future<Result<ScreenSelection>>;

  private:
    explicit ScreenCastPortal(DBusConnection* connection);

    auto Run(const ScreenCastOptions& options) -> Result<ScreenSelection>;
    auto AvailableCursorModes() -> Result<uint32_t>;
    auto Request(
        const std::string& method,
        const std::optional<std::string>& session_handle,
        bool with_parent_window,
        const PortalOptionWriter& write_options,
        const PortalResultVisitor& visit_results
    ) -> Result<>;
    auto WaitForResponse(const std::string& request_path, const PortalResultVisitor& visit_results) -> Result<>;

    static auto Connection() -> Result<DBusConnection*>;
    static auto NewToken() -> std::string;

    DBusConnection* m_connection;
    std::string m_sender;
};
