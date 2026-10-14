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

#include <filesystem>
#include <memory>
#include <string>

#include "Error.hpp"
#include "Process.hpp"

class IbusDaemon final
{
  public:
    static auto Start() -> Result<std::unique_ptr<IbusDaemon>>;

    ~IbusDaemon();
    IbusDaemon(const IbusDaemon&) = delete;
    auto operator=(const IbusDaemon&) -> IbusDaemon& = delete;

    [[nodiscard]] auto Address() const -> const std::string& { return m_address; }
    [[nodiscard]] auto Running() -> bool;
    [[nodiscard]] auto Listening() const -> bool;

  private:
    IbusDaemon(Process process, std::filesystem::path socket);

    Process m_process;
    std::filesystem::path m_socket;
    std::string m_address;
};
