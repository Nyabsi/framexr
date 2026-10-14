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

#include "IbusDaemon.hpp"

#include <csignal>
#include <format>
#include <system_error>
#include <utility>

#include "Platform.hpp"

auto IbusDaemon::Start() -> Result<std::unique_ptr<IbusDaemon>> {
    auto socket = Platform::RuntimeDirectory() / std::format("framexr-ibus-{}", Platform::ProcessId());
    ASSIGN_OR_RETURN(
        auto process,
        Process::Spawn(
            {"setpriv",
             "--pdeathsig",
             "TERM",
             "--",
             "ibus-daemon",
             "--panel=disable",
             "--emoji-extension=disable",
             std::format("--address=unix:path={}", socket.string())},
            {.set_env = {{"IBUS_ADDRESS_FILE", socket.string() + ".address"}}}
        )
    );
    return std::unique_ptr<IbusDaemon>(new IbusDaemon(std::move(process), std::move(socket)));
}

IbusDaemon::IbusDaemon(Process process, std::filesystem::path socket)
    : m_process(std::move(process)),
      m_socket(std::move(socket)),
      m_address(std::format("unix:path={}", m_socket.string())) {
}

IbusDaemon::~IbusDaemon() {
    m_process.Signal(SIGTERM);
    std::error_code error;
    std::filesystem::remove(m_socket, error);
    std::filesystem::remove(m_socket.string() + ".address", error);
}

auto IbusDaemon::Listening() const -> bool {
    std::error_code error;
    return std::filesystem::exists(m_socket, error);
}

auto IbusDaemon::Running() -> bool {
    const auto status = m_process.TryWait();
    return status && !*status;
}
