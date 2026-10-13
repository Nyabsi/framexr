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

#include "Notifications.hpp"

#include <chrono>
#include <set>
#include <string>
#include <utility>
#include <vector>

#define ____FIReference_1_boolean_INTERFACE_DEFINED__
#include <windows.h>

#include <asyncinfo.h>
#include <roapi.h>
#include <windows.applicationmodel.h>
#include <windows.ui.notifications.h>
#include <winstring.h>
#include <wrl/client.h>

#include "IUserNotificationListenerStatics.hpp"
#include "Logger.hpp"

namespace {

using ABI::Windows::Foundation::Collections::IVectorView;
using Microsoft::WRL::ComPtr;

constexpr INT32 kAccessUnspecified = 0;
constexpr INT32 kAccessAllowed = 1;
constexpr UINT32 kToastKind = 1;
constexpr auto kPollInterval = std::chrono::seconds(1);
constexpr auto kAsyncTimeout = std::chrono::seconds(10);

auto Utf8(HSTRING text) -> std::string {
    UINT32 length = 0;
    const wchar_t* raw = WindowsGetStringRawBuffer(text, &length);
    if (raw == nullptr || length == 0) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, raw, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, raw, static_cast<int>(length), utf8.data(), size, nullptr, nullptr);
    return utf8;
}

auto Take(HSTRING text) -> std::string {
    auto utf8 = Utf8(text);
    WindowsDeleteString(text);
    return utf8;
}

auto Wait(IInspectable* operation, const std::atomic<bool>& running) -> bool {
    ComPtr<IAsyncInfo> info;
    if (FAILED(operation->QueryInterface(__uuidof(IAsyncInfo), &info))) {
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + kAsyncTimeout;
    AsyncStatus status = AsyncStatus::Started;
    while (SUCCEEDED(info->get_Status(&status)) && status == AsyncStatus::Started) {
        if (!running.load(std::memory_order_relaxed) || std::chrono::steady_clock::now() > deadline) {
            info->Cancel();
            return false;
        }
        Sleep(10);
    }
    return status == AsyncStatus::Completed;
}

auto Listener(const std::atomic<bool>& running) -> ComPtr<IUserNotificationListener> {
    HSTRING class_id = nullptr;
    const std::wstring name = L"Windows.UI.Notifications.Management.UserNotificationListener";
    WindowsCreateString(name.c_str(), static_cast<UINT32>(name.size()), &class_id);
    ComPtr<IUserNotificationListenerStatics> statics;
    const HRESULT activated = RoGetActivationFactory(class_id, IUserNotificationListenerStatics::kIid, &statics);
    WindowsDeleteString(class_id);
    ComPtr<IUserNotificationListener> listener;
    if (FAILED(activated) || FAILED(statics->get_Current(&listener))) {
        LOG_WARN("Windows notification listener is unavailable (0x{:08x}).", static_cast<uint32_t>(activated));
        return nullptr;
    }
    INT32 access = kAccessUnspecified;
    listener->GetAccessStatus(&access);
    if (access == kAccessUnspecified) {
        ComPtr<IInspectable> request;
        if (SUCCEEDED(listener->RequestAccessAsync(&request))) {
            Wait(request.Get(), running);
        }
        listener->GetAccessStatus(&access);
    }
    if (access != kAccessAllowed) {
        LOG_WARN("Windows denied notification access. Allow it under Settings > Privacy & security > Notifications.");
        return nullptr;
    }
    return listener;
}

auto Snapshot(IUserNotificationListener& listener, const std::atomic<bool>& running)
    -> std::vector<ComPtr<ABI::Windows::UI::Notifications::IUserNotification>> {
    std::vector<ComPtr<ABI::Windows::UI::Notifications::IUserNotification>> notifications;
    ComPtr<IInspectable> request;
    if (FAILED(listener.GetNotificationsAsync(kToastKind, &request)) || !Wait(request.Get(), running)) {
        return notifications;
    }
    auto* operation = static_cast<ABI::Windows::Foundation::IAsyncOperation<IInspectable*>*>(request.Get());
    ComPtr<IInspectable> result;
    if (FAILED(operation->GetResults(&result)) || result == nullptr) {
        return notifications;
    }
    auto* list = static_cast<IVectorView<IInspectable*>*>(result.Get());
    UINT32 size = 0;
    list->get_Size(&size);
    for (UINT32 i = 0; i < size; ++i) {
        ComPtr<IInspectable> item;
        ComPtr<ABI::Windows::UI::Notifications::IUserNotification> notification;
        if (SUCCEEDED(list->GetAt(i, &item)) && SUCCEEDED(item.As(&notification))) {
            notifications.push_back(std::move(notification));
        }
    }
    return notifications;
}

auto ToToast(ABI::Windows::UI::Notifications::IUserNotification& notification) -> std::optional<ToastMessage> {
    std::string app;
    ComPtr<ABI::Windows::ApplicationModel::IAppInfo> info;
    ComPtr<ABI::Windows::ApplicationModel::IAppDisplayInfo> display;
    HSTRING app_name = nullptr;
    if (SUCCEEDED(notification.get_AppInfo(&info)) && info != nullptr && SUCCEEDED(info->get_DisplayInfo(&display))
        && display != nullptr && SUCCEEDED(display->get_DisplayName(&app_name))) {
        app = Take(app_name);
    }

    ComPtr<ABI::Windows::UI::Notifications::INotification> content;
    ComPtr<ABI::Windows::UI::Notifications::INotificationVisual> visual;
    ComPtr<ABI::Windows::UI::Notifications::INotificationBinding> binding;
    HSTRING template_name = nullptr;
    const std::wstring generic = L"ToastGeneric";
    WindowsCreateString(generic.c_str(), static_cast<UINT32>(generic.size()), &template_name);
    const bool bound = SUCCEEDED(notification.get_Notification(&content)) && content != nullptr
                    && SUCCEEDED(content->get_Visual(&visual)) && visual != nullptr
                    && SUCCEEDED(visual->GetBinding(template_name, &binding)) && binding != nullptr;
    WindowsDeleteString(template_name);
    if (!bound) {
        return std::nullopt;
    }
    ComPtr<IVectorView<ABI::Windows::UI::Notifications::AdaptiveNotificationText*>> elements;
    if (FAILED(binding->GetTextElements(&elements)) || elements == nullptr) {
        return std::nullopt;
    }
    std::vector<std::string> lines;
    UINT32 size = 0;
    elements->get_Size(&size);
    for (UINT32 i = 0; i < size; ++i) {
        ComPtr<ABI::Windows::UI::Notifications::IAdaptiveNotificationText> element;
        HSTRING text = nullptr;
        if (SUCCEEDED(elements->GetAt(i, &element)) && element != nullptr && SUCCEEDED(element->get_Text(&text))) {
            if (auto line = Take(text); !line.empty()) {
                lines.push_back(std::move(line));
            }
        }
    }
    if (lines.empty()) {
        return std::nullopt;
    }
    std::string body;
    for (size_t i = 1; i < lines.size(); ++i) {
        if (!body.empty()) {
            body += '\n';
        }
        body += lines[i];
    }
    return ToastMessage{
        .topic = ToastTopic::DesktopNotification,
        .title = lines.size() == 1 && !app.empty() ? app : lines.front(),
        .body = lines.size() == 1 && !app.empty() ? lines.front() : body,
        .opacity = 1.0f,
        .timeout = 5.0f,
    };
}

} // namespace

auto Notifications::RunDesktop() -> void {
    m_desktop_thread = std::thread([ingest = m_ingest, running = m_running] -> void {
        if (FAILED(RoInitialize(RO_INIT_MULTITHREADED))) {
            LOG_WARN("Could not initialise the Windows Runtime, desktop notifications are disabled.");
            return;
        }
        if (auto listener = Listener(*running)) {
            LOG_INFO("Listening to Windows notifications.");
            std::set<UINT32> seen;
            bool first = true;
            while (running->load(std::memory_order_relaxed)) {
                std::set<UINT32> current;
                for (const auto& notification : Snapshot(*listener.Get(), *running)) {
                    UINT32 id = 0;
                    notification->get_Id(&id);
                    current.insert(id);
                    if (first || seen.contains(id)) {
                        continue;
                    }
                    if (const auto toast = ToToast(*notification.Get())) {
                        ingest->Push(*toast);
                    }
                }
                seen = std::move(current);
                first = false;
                const auto wake = std::chrono::steady_clock::now() + kPollInterval;
                while (running->load(std::memory_order_relaxed) && std::chrono::steady_clock::now() < wake) {
                    Sleep(50);
                }
            }
        }
        RoUninitialize();
    });
}

auto Notifications::PollDesktop() -> void {
}

auto Notifications::CloseDesktop() -> void {
    if (m_desktop_thread.joinable()) {
        m_desktop_thread.join();
    }
}

auto Notifications::RunNative() -> void {
}
