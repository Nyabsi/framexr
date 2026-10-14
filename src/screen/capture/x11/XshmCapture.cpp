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

#include <cstdlib>
#include <format>
#include <mutex>
#include <optional>
#include <utility>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

#include "Logger.hpp"

namespace
{

auto MouseInScreen(::Display* display, const XshmScreen& screen, int image_width, int image_height) -> std::optional<MouseMeta>
{
    Window root = 0;
    Window child = 0;
    int root_x = 0;
    int root_y = 0;
    int window_x = 0;
    int window_y = 0;
    unsigned int mask = 0;
    if (XQueryPointer(display, XDefaultRootWindow(display), &root, &child, &root_x, &root_y, &window_x, &window_y, &mask) == 0)
    {
        return std::nullopt;
    }
    const int local_x = root_x - screen.x;
    const int local_y = root_y - screen.y;
    if (local_x < 0 || local_x > static_cast<int>(screen.width) || local_y < 0 || local_y > static_cast<int>(screen.height))
    {
        return std::nullopt;
    }
    return MouseMeta{
        .x = static_cast<float>(local_x) / static_cast<float>(image_width),
        .y = static_cast<float>(local_y) / static_cast<float>(image_height),
    };
}

} // namespace

XshmCapture::XshmCapture(std::shared_ptr<XshmScreen> screen) : m_screen(std::move(screen))
{
}

XshmCapture::~XshmCapture()
{
    {
        std::scoped_lock lock(m_request_mutex);
        m_stopping = true;
    }
    m_request_signal.notify_all();
    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

auto XshmCapture::Start(std::span<const DrmFormat>, FrameProcessor processor) -> void
{
    m_channel = std::make_shared<FrameChannel<std::unique_ptr<ProcessedFrame>>>(4);
    m_processor = std::move(processor);
    m_thread = std::thread(&XshmCapture::Run, this);
}

auto XshmCapture::Receive() -> std::unique_ptr<ProcessedFrame>
{
    if (!m_channel)
    {
        return nullptr;
    }
    auto frames = m_channel->TakeAll();
    if (frames.empty())
    {
        return nullptr;
    }
    return std::move(frames.back());
}

auto XshmCapture::Resume() -> void
{
    if (m_channel)
    {
        LOG_DEBUG("{}: dropped {} old frames before resuming", m_screen->name, m_channel->TakeAll().size());
    }
    RequestNewFrame();
}

auto XshmCapture::RequestNewFrame() -> void
{
    if (!m_channel)
    {
        return;
    }
    {
        std::scoped_lock lock(m_request_mutex);
        if (m_pending_requests >= 2)
        {
            LOG_DEBUG("Failed to send frame request: channel full");
            return;
        }
        ++m_pending_requests;
    }
    m_request_signal.notify_one();
}

auto XshmCapture::WaitForRequest() -> bool
{
    std::unique_lock lock(m_request_mutex);
    m_request_signal.wait(lock, [this]() -> bool { return m_stopping || m_pending_requests > 0; });
    if (m_stopping)
    {
        return false;
    }
    --m_pending_requests;
    return true;
}

auto XshmCapture::Run() -> void
{
    Xshm::EnsureThreads();
    const auto& name = m_screen->name;

    const char* display_name = std::getenv("DISPLAY");
    auto* display = display_name != nullptr ? XOpenDisplay(display_name) : nullptr;
    if (display == nullptr)
    {
        LOG_ERROR("{}: failed to open display", name);
        return;
    }

    const int screen = XDefaultScreen(display);
    XShmSegmentInfo segment{};
    XImage* image = nullptr;
    if (XShmQueryExtension(display) != 0)
    {
        image = XShmCreateImage(
            display,
            XDefaultVisual(display, screen),
            static_cast<unsigned int>(XDefaultDepth(display, screen)),
            ZPixmap,
            nullptr,
            &segment,
            m_screen->width,
            m_screen->height
        );
    }
    if (image != nullptr)
    {
        segment.shmid = shmget(
            IPC_PRIVATE,
            static_cast<size_t>(image->bytes_per_line) * static_cast<size_t>(image->height),
            IPC_CREAT | 0600
        );
    }
    if (image == nullptr || segment.shmid < 0)
    {
        LOG_ERROR("{}: failed to create shm", name);
        if (image != nullptr)
        {
            XDestroyImage(image);
        }
        XCloseDisplay(display);
        return;
    }
    segment.shmaddr = static_cast<char*>(shmat(segment.shmid, nullptr, 0));
    image->data = segment.shmaddr;
    segment.readOnly = False;
    if (XShmAttach(display, &segment) == 0)
    {
        LOG_ERROR("{}: failed to create shm", name);
        shmdt(segment.shmaddr);
        shmctl(segment.shmid, IPC_RMID, nullptr);
        XCloseDisplay(display);
        return;
    }

    const auto root = XDefaultRootWindow(display);
    while (WaitForRequest())
    {
        if (XShmGetImage(display, root, image, m_screen->x, m_screen->y, ~0UL) == 0)
        {
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
            .mouse = MouseInScreen(display, *m_screen, image->width, image->height),
        };
        LOG_TRACE("{}: captured frame", name);

        if (auto result = m_processor(Frame(frame)))
        {
            if (m_channel->Push(std::move(result)))
            {
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
