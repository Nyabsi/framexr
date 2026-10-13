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

#include <inspectable.h>

#include "IUserNotificationListener.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

class IUserNotificationListenerStatics : public IInspectable {
  public:
    static constexpr GUID kIid{0xff6123cf, 0x4386, 0x4aa3, {0xb7, 0x3d, 0xb8, 0x04, 0xe5, 0xb6, 0x3b, 0x23}};

    virtual HRESULT STDMETHODCALLTYPE get_Current(IUserNotificationListener** listener) = 0;

  protected:
    ~IUserNotificationListenerStatics() = default;
};

#pragma GCC diagnostic pop
