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

#include <eventtoken.h>
#include <inspectable.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

class IUserNotificationListener : public IInspectable
{
  public:
    static constexpr GUID kIid{0x62553e41, 0x8a06, 0x4cef, {0x82, 0x15, 0x60, 0x33, 0xa5, 0xbe, 0x4b, 0x03}};

    virtual HRESULT STDMETHODCALLTYPE RequestAccessAsync(IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAccessStatus(INT32* status) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_NotificationChanged(IInspectable* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_NotificationChanged(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNotificationsAsync(UINT32 kinds, IInspectable** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNotification(UINT32 id, IInspectable** notification) = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearNotifications() = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveNotification(UINT32 id) = 0;

  protected:
    ~IUserNotificationListener() = default;
};

#pragma GCC diagnostic pop
