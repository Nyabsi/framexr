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

#include "TsfTextStore.hpp"

#include <algorithm>
#include <utility>

#include <olectl.h>
#include <utility>

namespace {

constexpr TsViewCookie kView = 1;

} // namespace

TsfTextStore::TsfTextStore(HWND window) : m_window(window) {
}

auto TsfTextStore::Clear() -> void {
    const auto length = static_cast<LONG>(m_text.size());
    m_text.clear();
    m_selection_start = 0;
    m_selection_end = 0;
    if (m_sink && length > 0) {
        const TS_TEXTCHANGE change{.acpStart = 0, .acpOldEnd = length, .acpNewEnd = 0};
        m_sink->OnTextChange(0, &change);
        m_sink->OnSelectionChange();
    }
}

auto STDMETHODCALLTYPE TsfTextStore::QueryInterface(REFIID riid, void** object) -> HRESULT {
    if (object == nullptr) {
        return E_INVALIDARG;
    }
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITextStoreACP)) {
        *object = static_cast<ITextStoreACP*>(this);
    } else if (IsEqualIID(riid, IID_ITfContextOwnerCompositionSink)) {
        *object = static_cast<ITfContextOwnerCompositionSink*>(this);
    } else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::AddRef() -> ULONG {
    return ++m_references;
}

auto STDMETHODCALLTYPE TsfTextStore::Release() -> ULONG {
    const ULONG references = --m_references;
    if (references == 0) {
        delete this;
    }
    return references;
}

auto STDMETHODCALLTYPE TsfTextStore::AdviseSink(REFIID riid, IUnknown* sink, DWORD) -> HRESULT {
    if (!IsEqualIID(riid, IID_ITextStoreACPSink) || sink == nullptr) {
        return E_INVALIDARG;
    }
    if (m_sink) {
        return CONNECT_E_ADVISELIMIT;
    }
    return sink->QueryInterface(IID_PPV_ARGS(&m_sink));
}

auto STDMETHODCALLTYPE TsfTextStore::UnadviseSink(IUnknown*) -> HRESULT {
    m_sink.Reset();
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::RequestLock(DWORD flags, HRESULT* session) -> HRESULT {
    if (!m_sink || session == nullptr) {
        return E_UNEXPECTED;
    }
    if (m_lock != 0) {
        if ((flags & TS_LF_SYNC) != 0) {
            *session = TS_E_SYNCHRONOUS;
            return S_OK;
        }
        m_pending_lock = flags;
        *session = TS_S_ASYNC;
        return S_OK;
    }
    m_lock = flags;
    *session = m_sink->OnLockGranted(flags);
    m_lock = 0;
    if (const DWORD pending = std::exchange(m_pending_lock, 0); pending != 0) {
        m_lock = pending;
        m_sink->OnLockGranted(pending);
        m_lock = 0;
    }
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::GetStatus(TS_STATUS* status) -> HRESULT {
    if (status == nullptr) {
        return E_INVALIDARG;
    }
    status->dwDynamicFlags = 0;
    status->dwStaticFlags = TS_SS_NOHIDDENTEXT;
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::QueryInsert(LONG start, LONG end, ULONG, LONG* result_start, LONG* result_end) -> HRESULT {
    if (result_start == nullptr || result_end == nullptr) {
        return E_INVALIDARG;
    }
    *result_start = start;
    *result_end = end;
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::GetSelection(ULONG, ULONG count, TS_SELECTION_ACP* selection, ULONG* fetched) -> HRESULT {
    if (selection == nullptr || fetched == nullptr) {
        return E_INVALIDARG;
    }
    *fetched = 0;
    if (count == 0) {
        return S_OK;
    }
    selection->acpStart = m_selection_start;
    selection->acpEnd = m_selection_end;
    selection->style.ase = TS_AE_END;
    selection->style.fInterimChar = FALSE;
    *fetched = 1;
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::SetSelection(ULONG count, const TS_SELECTION_ACP* selection) -> HRESULT {
    if (count > 0 && selection != nullptr) {
        const auto size = static_cast<LONG>(m_text.size());
        m_selection_start = std::clamp(selection->acpStart, LONG{0}, size);
        m_selection_end = std::clamp(selection->acpEnd, m_selection_start, size);
    }
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::GetText(
    LONG start,
    LONG end,
    WCHAR* plain,
    ULONG plain_size,
    ULONG* plain_length,
    TS_RUNINFO* runs,
    ULONG runs_size,
    ULONG* runs_length,
    LONG* next
) -> HRESULT {
    const auto size = static_cast<LONG>(m_text.size());
    start = std::clamp(start, LONG{0}, size);
    end = end < 0 ? size : std::clamp(end, start, size);
    const auto copied = std::min(static_cast<ULONG>(end - start), plain_size);
    if (plain != nullptr && copied > 0) {
        std::copy_n(m_text.data() + start, copied, plain);
    }
    if (plain_length != nullptr) {
        *plain_length = plain != nullptr ? copied : 0;
    }
    if (runs_length != nullptr) {
        *runs_length = 0;
    }
    if (runs != nullptr && runs_size > 0 && runs_length != nullptr) {
        runs->uCount = copied;
        runs->type = TS_RT_PLAIN;
        *runs_length = 1;
    }
    if (next != nullptr) {
        *next = start + static_cast<LONG>(copied);
    }
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::SetText(DWORD, LONG start, LONG end, const WCHAR* text, ULONG length, TS_TEXTCHANGE* change)
    -> HRESULT {
    Replace(start, end, text, length, change);
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::GetFormattedText(LONG, LONG, IDataObject**) -> HRESULT {
    return E_NOTIMPL;
}

auto STDMETHODCALLTYPE TsfTextStore::GetEmbedded(LONG, REFGUID, REFIID, IUnknown**) -> HRESULT {
    return E_NOTIMPL;
}

auto STDMETHODCALLTYPE TsfTextStore::QueryInsertEmbedded(const GUID*, const FORMATETC*, BOOL* insertable) -> HRESULT {
    if (insertable != nullptr) {
        *insertable = FALSE;
    }
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::InsertEmbedded(DWORD, LONG, LONG, IDataObject*, TS_TEXTCHANGE*) -> HRESULT {
    return E_NOTIMPL;
}

auto STDMETHODCALLTYPE TsfTextStore::RequestSupportedAttrs(DWORD, ULONG, const TS_ATTRID*) -> HRESULT {
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::RequestAttrsAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) -> HRESULT {
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::RequestAttrsTransitioningAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) -> HRESULT {
    return S_OK;
}

auto STDMETHODCALLTYPE
TsfTextStore::FindNextAttrTransition(LONG, LONG halt, ULONG, const TS_ATTRID*, DWORD, LONG* next, BOOL* found, LONG* offset)
    -> HRESULT {
    if (next != nullptr) {
        *next = halt;
    }
    if (found != nullptr) {
        *found = FALSE;
    }
    if (offset != nullptr) {
        *offset = 0;
    }
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::RetrieveRequestedAttrs(ULONG, TS_ATTRVAL*, ULONG* fetched) -> HRESULT {
    if (fetched != nullptr) {
        *fetched = 0;
    }
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::GetEndACP(LONG* end) -> HRESULT {
    if (end == nullptr) {
        return E_INVALIDARG;
    }
    *end = static_cast<LONG>(m_text.size());
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::GetActiveView(TsViewCookie* view) -> HRESULT {
    if (view == nullptr) {
        return E_INVALIDARG;
    }
    *view = kView;
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::GetACPFromPoint(TsViewCookie, const POINT*, DWORD, LONG*) -> HRESULT {
    return E_NOTIMPL;
}

auto STDMETHODCALLTYPE TsfTextStore::GetTextExt(TsViewCookie, LONG, LONG, RECT* rect, BOOL* clipped) -> HRESULT {
    if (rect != nullptr) {
        *rect = RECT{};
    }
    if (clipped != nullptr) {
        *clipped = FALSE;
    }
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::GetScreenExt(TsViewCookie, RECT* rect) -> HRESULT {
    if (rect != nullptr) {
        *rect = RECT{};
    }
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::GetWnd(TsViewCookie, HWND* window) -> HRESULT {
    if (window == nullptr) {
        return E_INVALIDARG;
    }
    *window = m_window;
    return S_OK;
}

auto STDMETHODCALLTYPE
TsfTextStore::InsertTextAtSelection(DWORD flags, const WCHAR* text, ULONG length, LONG* start, LONG* end, TS_TEXTCHANGE* change)
    -> HRESULT {
    const LONG from = m_selection_start;
    if ((flags & TS_IAS_QUERYONLY) == 0) {
        Replace(m_selection_start, m_selection_end, text, length, change);
    }
    if (start != nullptr) {
        *start = from;
    }
    if (end != nullptr) {
        *end = from + static_cast<LONG>(length);
    }
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::InsertEmbeddedAtSelection(DWORD, IDataObject*, LONG*, LONG*, TS_TEXTCHANGE*) -> HRESULT {
    return E_NOTIMPL;
}

auto STDMETHODCALLTYPE TsfTextStore::OnStartComposition(ITfCompositionView*, BOOL* accept) -> HRESULT {
    if (accept != nullptr) {
        *accept = TRUE;
    }
    m_composing = true;
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::OnUpdateComposition(ITfCompositionView*, ITfRange*) -> HRESULT {
    return S_OK;
}

auto STDMETHODCALLTYPE TsfTextStore::OnEndComposition(ITfCompositionView*) -> HRESULT {
    m_composing = false;
    return S_OK;
}

auto TsfTextStore::Replace(LONG start, LONG end, const WCHAR* text, ULONG length, TS_TEXTCHANGE* change) -> void {
    const auto size = static_cast<LONG>(m_text.size());
    start = std::clamp(start, LONG{0}, size);
    end = std::clamp(end, start, size);
    m_text.replace(static_cast<size_t>(start), static_cast<size_t>(end - start), text != nullptr ? text : L"", length);
    m_selection_start = start + static_cast<LONG>(length);
    m_selection_end = m_selection_start;
    if (change != nullptr) {
        change->acpStart = start;
        change->acpOldEnd = end;
        change->acpNewEnd = start + static_cast<LONG>(length);
    }
}
