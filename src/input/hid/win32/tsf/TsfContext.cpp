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

#include "TsfContext.hpp"

#include <array>
#include <format>
#include <span>
#include <string_view>
#include <utility>

#include <windows.h>

#include <msctf.h>
#include <wrl/client.h>

#include "../Scancodes.hpp"
#include "TsfCandidateSink.hpp"
#include "TsfTextStore.hpp"
#include "Utf16.hpp"

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT kWake = WM_APP + 1;
constexpr DWORD kProfileEnabled = 0x2;
constexpr TfClientId kNoClient = 0;

struct ScriptLanguages {
    std::array<LANGID, 2> languages;
    std::wstring_view profile;
};

constexpr auto kScriptLanguages = std::to_array<ScriptLanguages>({
    {{}, {}},
    {{0x0412, 0}, {}},
    {{0x0411, 0}, {}},
    {{0x0404, 0x0C04}, L"ChangJie"},
});

auto Activate(ImeScript script) -> Result<> {
    ComPtr<ITfInputProcessorProfileMgr> manager;
    ComPtr<ITfInputProcessorProfiles> profiles;
    if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager)))
        || FAILED(manager.As(&profiles))) {
        return Unexpected("Text Services Framework is not available");
    }
    const auto& wanted = kScriptLanguages.at(static_cast<size_t>(script));
    for (const LANGID language : wanted.languages) {
        ComPtr<IEnumTfInputProcessorProfiles> list;
        if (language == 0 || FAILED(manager->EnumProfiles(language, &list))) {
            continue;
        }
        TF_INPUTPROCESSORPROFILE profile{};
        ULONG fetched = 0;
        while (list->Next(1, &profile, &fetched) == S_OK && fetched == 1) {
            if (profile.dwProfileType != TF_PROFILETYPE_INPUTPROCESSOR || (profile.dwFlags & kProfileEnabled) == 0) {
                continue;
            }
            if (!wanted.profile.empty()) {
                BSTR description = nullptr;
                profiles->GetLanguageProfileDescription(profile.clsid, language, profile.guidProfile, &description);
                const bool matches = description != nullptr && std::wstring_view(description).contains(wanted.profile);
                SysFreeString(description);
                if (!matches) {
                    continue;
                }
            }
            if (SUCCEEDED(
                    manager->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, language, profile.clsid, profile.guidProfile, nullptr, 0)
                )) {
                return {};
            }
        }
    }
    return Unexpected("Add this keyboard's input method in Windows Settings, Time and language, Language");
}

auto Open(ITfThreadMgr* threads, TfClientId client) -> void {
    ComPtr<ITfCompartmentMgr> compartments;
    ComPtr<ITfCompartment> open;
    if (FAILED(threads->QueryInterface(IID_PPV_ARGS(&compartments)))
        || FAILED(compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &open))) {
        return;
    }
    VARIANT value{};
    value.vt = VT_I4;
    value.lVal = TRUE;
    open->SetValue(client, &value);
}

auto KeyMessage(VirtualKey key) -> std::pair<WPARAM, LPARAM> {
    const auto scancode = Scancodes::ForKey(key).value_or(0);
    const UINT virtual_key = MapVirtualKeyW(scancode, MAPVK_VSC_TO_VK_EX);
    LPARAM parameter = 1 | (static_cast<LPARAM>(scancode & 0xFFU) << 16U);
    if ((scancode & kScancodeExtended) != 0) {
        parameter |= LPARAM{1} << 24U;
    }
    return {virtual_key, parameter};
}

auto SetModifiers(uint8_t modifiers) -> void {
    std::array<BYTE, 256> state{};
    const auto hold = [&state](std::span<const int> keys) -> void {
        for (const int key : keys) {
            state.at(static_cast<size_t>(key)) = 0x80;
        }
    };
    if ((modifiers & kModShift) != 0) {
        hold(std::array{VK_SHIFT, VK_LSHIFT});
    }
    if ((modifiers & kModCtrl) != 0) {
        hold(std::array{VK_CONTROL, VK_LCONTROL});
    }
    if ((modifiers & kModAlt) != 0) {
        hold(std::array{VK_MENU, VK_LMENU});
    }
    if ((modifiers & kModCapsLock) != 0) {
        state.at(VK_CAPITAL) = 1;
    }
    SetKeyboardState(state.data());
}

} // namespace

auto TsfContext::Create(ImeScript script) -> Result<std::unique_ptr<TsfContext>> {
    std::unique_ptr<TsfContext> context(new TsfContext(script));
    std::promise<Result<>> started;
    auto result = started.get_future();
    context->m_worker = std::thread([raw = context.get(), started = std::move(started)]() mutable -> void {
        raw->Run(std::move(started));
    });
    RETURN_IF_ERROR(result.get());
    return context;
}

TsfContext::TsfContext(ImeScript script) : m_script(script) {
}

TsfContext::~TsfContext() {
    if (m_thread_id != 0) {
        PostThreadMessageW(m_thread_id, WM_QUIT, 0, 0);
    }
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

auto TsfContext::Key(VirtualKey key, uint32_t keysym, uint8_t modifiers) -> bool {
    {
        const std::scoped_lock lock(m_mutex);
        m_keys.push_back({.key = key, .keysym = keysym, .modifiers = modifiers});
    }
    PostThreadMessageW(m_thread_id, kWake, 0, 0);
    return true;
}

auto TsfContext::Poll() -> void {
    TsfOutput output;
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_output.changed) {
            return;
        }
        output = std::exchange(m_output, {.preedit = m_output.preedit, .candidates = m_output.candidates});
    }
    m_preedit = std::move(output.preedit);
    m_candidates = std::move(output.candidates);
    for (const auto& committed : output.commits) {
        if (m_on_commit) {
            m_on_commit(committed);
        }
    }
    for (const uint32_t keysym : output.forwards) {
        if (m_on_forward) {
            m_on_forward(keysym);
        }
    }
    if (m_on_change) {
        m_on_change();
    }
}

auto TsfContext::Run(std::promise<Result<>> started) -> void {
    MSG message{};
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    m_thread_id = GetCurrentThreadId();
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        started.set_value(Unexpected("COM could not be initialized"));
        return;
    }
    ComPtr<ITfThreadMgrEx> threads;
    TfClientId client = kNoClient;
    if (FAILED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&threads)))
        || FAILED(threads->ActivateEx(&client, TF_TMAE_UIELEMENTENABLEDONLY))) {
        started.set_value(Unexpected("Text Services Framework could not be started"));
        CoUninitialize();
        return;
    }
    HWND window = CreateWindowExW(0, L"STATIC", L"FrameXR", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
    auto* store = new TsfTextStore(window);
    ComPtr<ITfDocumentMgr> document;
    ComPtr<ITfContext> context;
    ComPtr<ITfKeystrokeMgr> keystrokes;
    ComPtr<ITfUIElementMgr> elements;
    ComPtr<ITfSource> source;
    TfEditCookie edit = 0;
    DWORD sink_cookie = TF_INVALID_COOKIE;
    TsfCandidateSink* sink = nullptr;
    auto activated = Activate(m_script);
    if (activated
        && (FAILED(threads->CreateDocumentMgr(&document))
            || FAILED(document->CreateContext(client, 0, static_cast<ITextStoreACP*>(store), &context, &edit))
            || FAILED(document->Push(context.Get())) || FAILED(threads->SetFocus(document.Get())) || FAILED(threads.As(&keystrokes))
            || FAILED(threads.As(&elements)) || FAILED(threads.As(&source)))) {
        activated = Unexpected("The input method could not be attached");
    }
    if (activated) {
        sink = new TsfCandidateSink(elements);
        source->AdviseSink(IID_ITfUIElementSink, static_cast<ITfUIElementSink*>(sink), &sink_cookie);
        Open(threads.Get(), client);
    }
    const bool running = activated.has_value();
    started.set_value(std::move(activated));

    while (running && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message != kWake) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            continue;
        }
        std::deque<TsfKey> keys;
        {
            const std::scoped_lock lock(m_mutex);
            keys.swap(m_keys);
        }
        for (const auto& key : keys) {
            const auto [virtual_key, parameter] = KeyMessage(key.key);
            SetModifiers(key.modifiers);
            BOOL eaten = FALSE;
            if (virtual_key != 0 && SUCCEEDED(keystrokes->TestKeyDown(virtual_key, parameter, &eaten)) && eaten != FALSE) {
                keystrokes->KeyDown(virtual_key, parameter, &eaten);
                keystrokes->KeyUp(virtual_key, parameter | (LPARAM{3} << 30U), &eaten);
            }
            const std::scoped_lock lock(m_mutex);
            if (eaten == FALSE) {
                m_output.forwards.push_back(key.keysym);
            }
            if (!store->Composing() && !store->Text().empty()) {
                m_output.commits.push_back(Utf16::Narrow(store->Text()));
                store->Clear();
            }
            m_output.preedit = store->Composing() ? Utf16::Narrow(store->Text()) : std::string();
            m_output.candidates = sink != nullptr ? sink->Candidates() : std::vector<std::string>();
            m_output.changed = true;
        }
    }

    if (sink != nullptr) {
        source->UnadviseSink(sink_cookie);
        sink->Release();
    }
    if (document) {
        document->Pop(TF_POPF_ALL);
    }
    context.Reset();
    document.Reset();
    store->Release();
    threads->Deactivate();
    threads.Reset();
    DestroyWindow(window);
    CoUninitialize();
}
