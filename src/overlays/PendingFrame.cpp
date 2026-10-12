// FrameXR
//
// This is a port of wlx-overlay-s from Rust to C++.
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
// Copyright (C) 2023-2025 galister and wlx-overlay-s contributors
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

#include "PendingFrame.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>
#include <limits>
#include <numbers>
#include <ranges>
#include <utility>

#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "CommandBatch.hpp"
#include "CommandBuffer.hpp"
#include "Config.hpp"
#include "DbusNotificationSender.hpp"
#include "Device.hpp"
#include "Hid.hpp"
#include "Image.hpp"
#include "Input.hpp"
#include "Logger.hpp"
#include "OverlayContainer.hpp"
#include "OverlayRenderer.hpp"
#include "Pipeline.hpp"
#include "ScreenInteraction.hpp"
#include "ScreenRenderer.hpp"
#include "Shaders.hpp"
#include "SplitOverlayBackend.hpp"
#include "Yaml.hpp"
#include "framexr_config.h"

#if FRAMEXR_FEATURE_WAYLAND
#include "Notifications.hpp"
#include "PipewireCapture.hpp"
#include "WaylandClient.hpp"
#include "WlrDmabufCapture.hpp"
#include "WlrScreencopyCapture.hpp"
#endif

#if FRAMEXR_FEATURE_X11
#include "XshmCapture.hpp"
#endif

#ifdef _WIN32
#include "DxgiCapture.hpp"
#endif

PendingFrame::PendingFrame(Frame&& source) : m_source(std::move(source)) {
#ifndef _WIN32
    if (auto* dmabuf = std::get_if<DmabufFrame>(&m_source)) {
        for (auto& plane : dmabuf->planes) {
            plane.fd = plane.fd >= 0 ? dup(plane.fd) : -1;
        }
    } else if (auto* memfd = std::get_if<MemFdFrame>(&m_source)) {
        memfd->plane.fd = memfd->plane.fd >= 0 ? dup(memfd->plane.fd) : -1;
    } else
#endif
        if (auto* memptr = std::get_if<MemPtrFrame>(&m_source); memptr != nullptr && memptr->data != nullptr) {
        m_bytes.assign(memptr->data, memptr->data + memptr->size);
        memptr->data = m_bytes.data();
    }
}

PendingFrame::~PendingFrame() {
#ifndef _WIN32
    if (const auto* dmabuf = std::get_if<DmabufFrame>(&m_source)) {
        for (const auto& plane : dmabuf->planes) {
            if (plane.fd >= 0) {
                close(plane.fd);
            }
        }
    } else if (const auto* memfd = std::get_if<MemFdFrame>(&m_source); memfd != nullptr && memfd->plane.fd >= 0) {
        close(memfd->plane.fd);
    }
#endif
}
