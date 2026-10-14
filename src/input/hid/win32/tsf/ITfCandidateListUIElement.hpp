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

#include <windows.h>

#include <msctf.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

class ITfCandidateListUIElement : public ITfUIElement {
  public:
    static constexpr GUID kIid{0xea1ea138, 0x19df, 0x11d7, {0xa6, 0xd2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};

    virtual auto STDMETHODCALLTYPE GetUpdatedFlags(DWORD* flags) -> HRESULT = 0;
    virtual auto STDMETHODCALLTYPE GetDocumentMgr(ITfDocumentMgr** document) -> HRESULT = 0;
    virtual auto STDMETHODCALLTYPE GetCount(UINT* count) -> HRESULT = 0;
    virtual auto STDMETHODCALLTYPE GetSelection(UINT* index) -> HRESULT = 0;
    virtual auto STDMETHODCALLTYPE GetString(UINT index, BSTR* text) -> HRESULT = 0;
    virtual auto STDMETHODCALLTYPE GetPageIndex(UINT* indices, UINT size, UINT* pages) -> HRESULT = 0;
    virtual auto STDMETHODCALLTYPE SetPageIndex(UINT* indices, UINT pages) -> HRESULT = 0;
    virtual auto STDMETHODCALLTYPE GetCurrentPage(UINT* page) -> HRESULT = 0;

  protected:
    ~ITfCandidateListUIElement() = default;
};

#pragma GCC diagnostic pop
