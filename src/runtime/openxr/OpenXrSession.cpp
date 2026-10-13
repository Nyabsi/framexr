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

#include "OpenXrSession.hpp"

#include "framexr_config.h"

#if FRAMEXR_FEATURE_OSC
#endif

auto OpenXrSession::Create(const OpenXrInstance& instance) -> Result<std::unique_ptr<OpenXrSession>> {
    ASSIGN_OR_RETURN(const auto handle, instance.CreateOverlaySession());
    auto session = std::unique_ptr<OpenXrSession>(new OpenXrSession(handle));
    ASSIGN_OR_RETURN(session->m_stage, CreateReferenceSpace(instance, handle, XR_REFERENCE_SPACE_TYPE_STAGE));
    ASSIGN_OR_RETURN(session->m_view, CreateReferenceSpace(instance, handle, XR_REFERENCE_SPACE_TYPE_VIEW));
    return session;
}

OpenXrSession::OpenXrSession(XrSession session) : m_session(session) {
}

OpenXrSession::~OpenXrSession() {
    if (m_view != XR_NULL_HANDLE) {
        xrDestroySpace(m_view);
    }
    if (m_stage != XR_NULL_HANDLE) {
        xrDestroySpace(m_stage);
    }
    xrDestroySession(m_session);
}

auto OpenXrSession::CreateReferenceSpace(const OpenXrInstance& instance, XrSession session, XrReferenceSpaceType type)
    -> Result<XrSpace> {
    const XrReferenceSpaceCreateInfo create_info{
        .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
        .referenceSpaceType = type,
        .poseInReferenceSpace = {.orientation = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f}, .position = {}},
    };
    XrSpace space = XR_NULL_HANDLE;
    RETURN_IF_ERROR(instance.Check(xrCreateReferenceSpace(session, &create_info, &space)));
    return space;
}
