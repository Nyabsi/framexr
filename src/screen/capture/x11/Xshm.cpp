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

#include "Xshm.hpp"

#include <cstdlib>
#include <mutex>

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include "Error.hpp"

namespace {

auto ReplaceAll(std::string text, std::string_view from, std::string_view to) -> std::string {
    for (size_t pos = text.find(from); pos != std::string::npos; pos = text.find(from, pos + to.size())) {
        text.replace(pos, from.size(), to);
    }
    return text;
}

} // namespace

auto Xshm::Monitors() -> Result<std::vector<std::shared_ptr<XshmScreen>>> {
    Xshm::EnsureThreads();
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

auto Xshm::EnsureThreads() -> void {
    static std::once_flag once;
    std::call_once(once, []() -> void { XInitThreads(); });
}
