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

#include "TsfCandidateSink.hpp"

#include <utility>

#include "ITfCandidateListUIElement.hpp"
#include "Utf16.hpp"

TsfCandidateSink::TsfCandidateSink(Microsoft::WRL::ComPtr<ITfUIElementMgr> elements) : m_elements(std::move(elements))
{
}

auto STDMETHODCALLTYPE TsfCandidateSink::QueryInterface(REFIID riid, void** object) -> HRESULT
{
    if (object == nullptr)
    {
        return E_INVALIDARG;
    }
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfUIElementSink))
    {
        *object = static_cast<ITfUIElementSink*>(this);
        AddRef();
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

auto STDMETHODCALLTYPE TsfCandidateSink::AddRef() -> ULONG
{
    return ++m_references;
}

auto STDMETHODCALLTYPE TsfCandidateSink::Release() -> ULONG
{
    const ULONG references = --m_references;
    if (references == 0)
    {
        delete this;
    }
    return references;
}

auto STDMETHODCALLTYPE TsfCandidateSink::BeginUIElement(DWORD id, BOOL* show) -> HRESULT
{
    if (show != nullptr)
    {
        *show = FALSE;
    }
    return UpdateUIElement(id);
}

auto STDMETHODCALLTYPE TsfCandidateSink::UpdateUIElement(DWORD id) -> HRESULT
{
    Microsoft::WRL::ComPtr<ITfUIElement> element;
    Microsoft::WRL::ComPtr<ITfCandidateListUIElement> list;
    if (FAILED(m_elements->GetUIElement(id, &element))
        || FAILED(element->QueryInterface(ITfCandidateListUIElement::kIid, reinterpret_cast<void**>(list.GetAddressOf()))))
    {
        return S_OK;
    }
    UINT count = 0;
    UINT page = 0;
    UINT pages = 0;
    list->GetCount(&count);
    list->GetCurrentPage(&page);
    list->GetPageIndex(nullptr, 0, &pages);
    std::vector<UINT> starts(pages);
    if (pages > 0)
    {
        list->GetPageIndex(starts.data(), pages, &pages);
    }
    const UINT first = page < pages ? starts[page] : 0;
    const UINT last = page + 1 < pages ? starts[page + 1] : count;
    m_candidates.clear();
    for (UINT index = first; index < last; ++index)
    {
        BSTR text = nullptr;
        if (SUCCEEDED(list->GetString(index, &text)) && text != nullptr)
        {
            m_candidates.push_back(Utf16::Narrow(text));
            SysFreeString(text);
        }
    }
    return S_OK;
}

auto STDMETHODCALLTYPE TsfCandidateSink::EndUIElement(DWORD) -> HRESULT
{
    m_candidates.clear();
    return S_OK;
}
