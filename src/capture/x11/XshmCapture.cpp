// FrameXR
//
// This is a port of wlx-capture from Rust to C++.
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
// Copyright (C) 2023-2025 galister and wlx-capture contributors
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

#include "XshmCapture.hpp"

#include <sys/ipc.h>
#include <sys/shm.h>

#include <cstdlib>
#include <format>
#include <mutex>
#include <optional>
#include <utility>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xrandr.h>

#include "Logger.hpp"

namespace {

auto EnsureXlibThreads() -> void {
    static std::once_flag once;
    std::call_once(once, []() -> void { XInitThreads(); });
}

auto ReplaceAll(std::string text, std::string_view from, std::string_view to) -> std::string {
    for (size_t pos = text.find(from); pos != std::string::npos; pos = text.find(from, pos + to.size())) {
        text.replace(pos, from.size(), to);
    }
    return text;
}

auto MouseInScreen(::Display* display, const XshmScreen& screen, int image_width, int image_height) -> std::optional<MouseMeta> {
    Window root = 0;
    Window child = 0;
    int root_x = 0;
    int root_y = 0;
    int window_x = 0;
    int window_y = 0;
    unsigned int mask = 0;
    if (XQueryPointer(display, XDefaultRootWindow(display), &root, &child, &root_x, &root_y, &window_x, &window_y, &mask) == 0) {
        return std::nullopt;
    }
    const int local_x = root_x - screen.x;
    const int local_y = root_y - screen.y;
    if (local_x < 0 || local_x > static_cast<int>(screen.width) || local_y < 0 || local_y > static_cast<int>(screen.height)) {
        return std::nullopt;
    }
    return MouseMeta{
        .x = static_cast<float>(local_x) / static_cast<float>(image_width),
        .y = static_cast<float>(local_y) / static_cast<float>(image_height),
    };
}

} // namespace

auto Xshm::Monitors() -> Result<std::vector<std::shared_ptr<XshmScreen>>> {
    EnsureXlibThreads();
    const char* display_name = std::getenv("DISPLAY");
    if (display_name == nullptr) {
        return Unexpected("environment variable not found");
    }
    auto* display = XOpenDisplay(display_name);
    if (display == nullptr) {
        return Unexpected("X11: Failed to open display");
    }

    int count = 0;
    auto* monitors = XRRGetMonitors(display, XDefaultRootWindow(display), False, &count);
    std::vector<std::shared_ptr<XshmScreen>> screens;
    for (int index = 0; index < count; ++index) {
        const auto& monitor = monitors[index];
        char* atom_name = XGetAtomName(display, monitor.name);
        screens.push_back(
            std::make_shared<XshmScreen>(XshmScreen{
                .name = ReplaceAll(atom_name != nullptr ? atom_name : "", "DisplayPort", "DP"),
                .x = monitor.x,
                .y = monitor.y,
                .width = static_cast<uint32_t>(monitor.width),
                .height = static_cast<uint32_t>(monitor.height),
            })
        );
        if (atom_name != nullptr) {
            XFree(atom_name);
        }
    }
    if (monitors != nullptr) {
        XRRFreeMonitors(monitors);
    }
    XCloseDisplay(display);
    return screens;
}

XshmCapture::XshmCapture(std::shared_ptr<XshmScreen> screen) : screen_(std::move(screen)) {
}

XshmCapture::~XshmCapture() {
    {
        std::scoped_lock lock(request_mutex_);
        stopping_ = true;
    }
    request_signal_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

auto XshmCapture::Start(std::span<const DrmFormat>, FrameProcessor processor) -> void {
    channel_ = std::make_shared<FrameChannel<std::unique_ptr<ProcessedFrame>>>(4);
    processor_ = std::move(processor);
    thread_ = std::thread(&XshmCapture::Run, this);
}

auto XshmCapture::Receive() -> std::unique_ptr<ProcessedFrame> {
    if (!channel_) {
        return nullptr;
    }
    auto frames = channel_->TakeAll();
    if (frames.empty()) {
        return nullptr;
    }
    return std::move(frames.back());
}

auto XshmCapture::Resume() -> void {
    if (channel_) {
        LOG_DEBUG("{}: dropped {} old frames before resuming", screen_->name, channel_->TakeAll().size());
    }
    RequestNewFrame();
}

auto XshmCapture::RequestNewFrame() -> void {
    if (!channel_) {
        return;
    }
    {
        std::scoped_lock lock(request_mutex_);
        if (pending_requests_ >= 2) {
            LOG_DEBUG("Failed to send frame request: channel full");
            return;
        }
        ++pending_requests_;
    }
    request_signal_.notify_one();
}

auto XshmCapture::WaitForRequest() -> bool {
    std::unique_lock lock(request_mutex_);
    request_signal_.wait(lock, [this]() -> bool { return stopping_ || pending_requests_ > 0; });
    if (stopping_) {
        return false;
    }
    --pending_requests_;
    return true;
}

auto XshmCapture::Run() -> void {
    EnsureXlibThreads();
    const auto& name = screen_->name;

    const char* display_name = std::getenv("DISPLAY");
    auto* display = display_name != nullptr ? XOpenDisplay(display_name) : nullptr;
    if (display == nullptr) {
        LOG_ERROR("{}: failed to open display", name);
        return;
    }

    const int screen = XDefaultScreen(display);
    XShmSegmentInfo segment{};
    XImage* image = nullptr;
    if (XShmQueryExtension(display) != 0) {
        image = XShmCreateImage(
            display,
            XDefaultVisual(display, screen),
            static_cast<unsigned int>(XDefaultDepth(display, screen)),
            ZPixmap,
            nullptr,
            &segment,
            screen_->width,
            screen_->height
        );
    }
    if (image != nullptr) {
        segment.shmid = shmget(
            IPC_PRIVATE,
            static_cast<size_t>(image->bytes_per_line) * static_cast<size_t>(image->height),
            IPC_CREAT | 0600
        );
    }
    if (image == nullptr || segment.shmid < 0) {
        LOG_ERROR("{}: failed to create shm", name);
        if (image != nullptr) {
            XDestroyImage(image);
        }
        XCloseDisplay(display);
        return;
    }
    segment.shmaddr = static_cast<char*>(shmat(segment.shmid, nullptr, 0));
    image->data = segment.shmaddr;
    segment.readOnly = False;
    if (XShmAttach(display, &segment) == 0) {
        LOG_ERROR("{}: failed to create shm", name);
        shmdt(segment.shmaddr);
        shmctl(segment.shmid, IPC_RMID, nullptr);
        XCloseDisplay(display);
        return;
    }

    const auto root = XDefaultRootWindow(display);
    while (WaitForRequest()) {
        if (XShmGetImage(display, root, image, screen_->x, screen_->y, ~0UL) == 0) {
            LOG_DEBUG("{}: XShmGetImage failed", name);
            continue;
        }

        MemPtrFrame frame{
            .format =
                {
                    .width = static_cast<uint32_t>(image->width),
                    .height = static_cast<uint32_t>(image->height),
                    .fourcc = kDrmFormatXrgb8888,
                },
            .data = reinterpret_cast<const std::byte*>(image->data),
            .size = static_cast<size_t>(image->bytes_per_line) * static_cast<size_t>(image->height),
            .mouse = MouseInScreen(display, *screen_, image->width, image->height),
        };
        LOG_TRACE("{}: captured frame", name);

        if (auto result = processor_(Frame(frame))) {
            if (channel_->Push(std::move(result))) {
                LOG_DEBUG("{}: channel full", name);
            }
        }
    }

    LOG_WARN("{}: capture thread channel closed (recv)", name);
    XShmDetach(display, &segment);
    XDestroyImage(image);
    shmdt(segment.shmaddr);
    shmctl(segment.shmid, IPC_RMID, nullptr);
    XCloseDisplay(display);
    LOG_WARN("{}: capture thread stopped", name);
}
