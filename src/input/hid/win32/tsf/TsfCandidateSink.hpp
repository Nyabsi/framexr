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
#include <vector>

#include <windows.h>

#include <msctf.h>
#include <wrl/client.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

class TsfCandidateSink final : public ITfUIElementSink {
  public:
    explicit TsfCandidateSink(Microsoft::WRL::ComPtr<ITfUIElementMgr> elements);
    TsfCandidateSink(const TsfCandidateSink&) = delete;
    auto operator=(const TsfCandidateSink&) -> TsfCandidateSink& = delete;

    [[nodiscard]] auto Candidates() const -> const std::vector<std::string>& { return m_candidates; }

    auto STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) -> HRESULT override;
    auto STDMETHODCALLTYPE AddRef() -> ULONG override;
    auto STDMETHODCALLTYPE Release() -> ULONG override;

    auto STDMETHODCALLTYPE BeginUIElement(DWORD id, BOOL* show) -> HRESULT override;
    auto STDMETHODCALLTYPE UpdateUIElement(DWORD id) -> HRESULT override;
    auto STDMETHODCALLTYPE EndUIElement(DWORD id) -> HRESULT override;

  private:
    ~TsfCandidateSink() = default;

    std::atomic<ULONG> m_references = 1;
    Microsoft::WRL::ComPtr<ITfUIElementMgr> m_elements;
    std::vector<std::string> m_candidates;
};

#pragma GCC diagnostic pop
