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

#include "ImeContext.hpp"

#include "ibus/IbusContext.hpp"
#include "ibus/IbusDaemon.hpp"

auto ImeContext::Connect(ImeScript script) -> Result<std::unique_ptr<ImeContext>>
{
    static std::unique_ptr<IbusDaemon> daemon;
    if (!daemon)
    {
        ASSIGN_OR_RETURN(daemon, IbusDaemon::Start());
    }
    if (!daemon->Running())
    {
        daemon.reset();
        return Unexpected("IBus could not be started, install ibus and an input method for this script");
    }
    if (!daemon->Listening())
    {
        return std::unique_ptr<ImeContext>();
    }
    ASSIGN_OR_RETURN(auto context, IbusContext::Create(daemon->Address(), script));
    return std::unique_ptr<ImeContext>(std::move(context));
}
