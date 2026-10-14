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

#include <atomic>
#include <string>

#include <windows.h>

#include <msctf.h>
#include <textstor.h>
#include <wrl/client.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

class TsfTextStore final : public ITextStoreACP, public ITfContextOwnerCompositionSink {
  public:
    explicit TsfTextStore(HWND window);
    TsfTextStore(const TsfTextStore&) = delete;
    auto operator=(const TsfTextStore&) -> TsfTextStore& = delete;

    [[nodiscard]] auto Text() const -> const std::wstring& { return m_text; }
    [[nodiscard]] auto Composing() const -> bool { return m_composing; }
    auto Clear() -> void;

    auto STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) -> HRESULT override;
    auto STDMETHODCALLTYPE AddRef() -> ULONG override;
    auto STDMETHODCALLTYPE Release() -> ULONG override;

    auto STDMETHODCALLTYPE AdviseSink(REFIID riid, IUnknown* sink, DWORD mask) -> HRESULT override;
    auto STDMETHODCALLTYPE UnadviseSink(IUnknown* sink) -> HRESULT override;
    auto STDMETHODCALLTYPE RequestLock(DWORD flags, HRESULT* session) -> HRESULT override;
    auto STDMETHODCALLTYPE GetStatus(TS_STATUS* status) -> HRESULT override;
    auto STDMETHODCALLTYPE QueryInsert(LONG start, LONG end, ULONG length, LONG* result_start, LONG* result_end)
        -> HRESULT override;
    auto STDMETHODCALLTYPE GetSelection(ULONG index, ULONG count, TS_SELECTION_ACP* selection, ULONG* fetched) -> HRESULT override;
    auto STDMETHODCALLTYPE SetSelection(ULONG count, const TS_SELECTION_ACP* selection) -> HRESULT override;
    auto STDMETHODCALLTYPE GetText(
        LONG start,
        LONG end,
        WCHAR* plain,
        ULONG plain_size,
        ULONG* plain_length,
        TS_RUNINFO* runs,
        ULONG runs_size,
        ULONG* runs_length,
        LONG* next
    ) -> HRESULT override;
    auto STDMETHODCALLTYPE SetText(DWORD flags, LONG start, LONG end, const WCHAR* text, ULONG length, TS_TEXTCHANGE* change)
        -> HRESULT override;
    auto STDMETHODCALLTYPE GetFormattedText(LONG start, LONG end, IDataObject** object) -> HRESULT override;
    auto STDMETHODCALLTYPE GetEmbedded(LONG position, REFGUID service, REFIID riid, IUnknown** object) -> HRESULT override;
    auto STDMETHODCALLTYPE QueryInsertEmbedded(const GUID* service, const FORMATETC* format, BOOL* insertable) -> HRESULT override;
    auto STDMETHODCALLTYPE InsertEmbedded(DWORD flags, LONG start, LONG end, IDataObject* object, TS_TEXTCHANGE* change)
        -> HRESULT override;
    auto STDMETHODCALLTYPE RequestSupportedAttrs(DWORD flags, ULONG count, const TS_ATTRID* attributes) -> HRESULT override;
    auto STDMETHODCALLTYPE RequestAttrsAtPosition(LONG position, ULONG count, const TS_ATTRID* attributes, DWORD flags)
        -> HRESULT override;
    auto STDMETHODCALLTYPE RequestAttrsTransitioningAtPosition(LONG position, ULONG count, const TS_ATTRID* attributes, DWORD flags)
        -> HRESULT override;
    auto STDMETHODCALLTYPE FindNextAttrTransition(
        LONG start,
        LONG halt,
        ULONG count,
        const TS_ATTRID* attributes,
        DWORD flags,
        LONG* next,
        BOOL* found,
        LONG* offset
    ) -> HRESULT override;
    auto STDMETHODCALLTYPE RetrieveRequestedAttrs(ULONG count, TS_ATTRVAL* values, ULONG* fetched) -> HRESULT override;
    auto STDMETHODCALLTYPE GetEndACP(LONG* end) -> HRESULT override;
    auto STDMETHODCALLTYPE GetActiveView(TsViewCookie* view) -> HRESULT override;
    auto STDMETHODCALLTYPE GetACPFromPoint(TsViewCookie view, const POINT* point, DWORD flags, LONG* position) -> HRESULT override;
    auto STDMETHODCALLTYPE GetTextExt(TsViewCookie view, LONG start, LONG end, RECT* rect, BOOL* clipped) -> HRESULT override;
    auto STDMETHODCALLTYPE GetScreenExt(TsViewCookie view, RECT* rect) -> HRESULT override;
    auto STDMETHODCALLTYPE GetWnd(TsViewCookie view, HWND* window) -> HRESULT override;
    auto STDMETHODCALLTYPE
    InsertTextAtSelection(DWORD flags, const WCHAR* text, ULONG length, LONG* start, LONG* end, TS_TEXTCHANGE* change)
        -> HRESULT override;
    auto STDMETHODCALLTYPE InsertEmbeddedAtSelection(DWORD flags, IDataObject* object, LONG* start, LONG* end, TS_TEXTCHANGE* change)
        -> HRESULT override;

    auto STDMETHODCALLTYPE OnStartComposition(ITfCompositionView* composition, BOOL* accept) -> HRESULT override;
    auto STDMETHODCALLTYPE OnUpdateComposition(ITfCompositionView* composition, ITfRange* range) -> HRESULT override;
    auto STDMETHODCALLTYPE OnEndComposition(ITfCompositionView* composition) -> HRESULT override;

  private:
    ~TsfTextStore() = default;

    auto Replace(LONG start, LONG end, const WCHAR* text, ULONG length, TS_TEXTCHANGE* change) -> void;

    std::atomic<ULONG> m_references = 1;
    HWND m_window;
    std::wstring m_text;
    LONG m_selection_start = 0;
    LONG m_selection_end = 0;
    Microsoft::WRL::ComPtr<ITextStoreACPSink> m_sink;
    DWORD m_lock = 0;
    DWORD m_pending_lock = 0;
    bool m_composing = false;
};

#pragma GCC diagnostic pop
