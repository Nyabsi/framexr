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

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <dbus/dbus.h>

#include "Error.hpp"
#include "ImeContext.hpp"

class IbusContext final : public ImeContext
{
  public:
    static auto Create(const std::string& address, ImeScript script) -> Result<std::unique_ptr<IbusContext>>;

    ~IbusContext() override;
    IbusContext(const IbusContext&) = delete;
    auto operator=(const IbusContext&) -> IbusContext& = delete;

    auto Key(VirtualKey key, uint32_t keysym, uint8_t modifiers) -> bool override;
    auto Poll() -> void override;

  private:
    IbusContext(DBusConnection* connection, std::string path);

    auto Call(const char* interface, const char* method, const std::function<void(DBusMessageIter&)>& append)
        -> Result<DBusMessage*>;
    auto Dispatch(DBusMessage* message) -> void;
    auto SelectEngine(ImeScript script) -> Result<>;
    auto Selected(const std::string& name) -> bool;

    DBusConnection* m_connection;
    std::string m_path;
};
