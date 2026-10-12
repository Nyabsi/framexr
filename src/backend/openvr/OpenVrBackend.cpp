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

#include "OpenVrBackend.hpp"

#include <format>

#include <openvr.h>

#include "Logger.hpp"
#include "OpenVrManifest.hpp"
#include "OpenVrRuntime.hpp"
#include "OpenVrSession.hpp"
#include "framexr_config.h"

#if FRAMEXR_FEATURE_OSC
#endif

namespace {

auto ToBackendError(const Error& error) -> std::unexpected<BackendError> {
    return std::unexpected(BackendError{.failure = BackendFailure::Fatal, .message = error.Message()});
}

} // namespace

auto OpenVrBackend::Uninstall() -> void {
    vr::EVRInitError error = vr::VRInitError_None;
    vr::VR_Init(&error, vr::VRApplication_Overlay);
    if (error != vr::VRInitError_None) {
        LOG_ERROR("Uninstall failed: could not reach OpenVR");
        return;
    }
    const OpenVrRuntime runtime;
    if (auto result = OpenVrManifest::Uninstall(); !result) {
        LOG_DEBUG("{}", result.error().Message());
    }
}

auto OpenVrBackend::Run(std::atomic<bool>& running, bool show_by_default) -> BackendResult {
    vr::EVRInitError error = vr::VRInitError_None;
    vr::VR_Init(&error, vr::VRApplication_Overlay);
    if (error != vr::VRInitError_None) {
        LOG_WARN("Will not use OpenVR: Context init failed");
        return std::unexpected(BackendError{.failure = BackendFailure::NotSupported, .message = "backend not supported"});
    }
    OpenVrRuntime runtime;
    LOG_INFO("Using OpenVR runtime");

    auto session = OpenVrSession::Create(runtime, show_by_default);
    if (!session) {
        return ToBackendError(session.error());
    }
    if (auto result = (*session)->Run(running); !result) {
        return ToBackendError(result.error());
    }
    LOG_WARN("OpenVR shutdown");
    return {};
}
