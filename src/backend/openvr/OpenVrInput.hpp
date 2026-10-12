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

#pragma once

#include <array>
#include <optional>

#include <openvr.h>

#include "Error.hpp"
#include "Input.hpp"

struct OpenVrHandSource {
    std::optional<vr::TrackedDeviceIndex_t> device;
    vr::VRInputValueHandle_t input = vr::k_ulInvalidInputValueHandle;
    vr::VRActionHandle_t pose = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t haptics = vr::k_ulInvalidActionHandle;
};

struct OpenVrActions {
    vr::VRActionSetHandle_t set = vr::k_ulInvalidActionSetHandle;
    vr::VRActionHandle_t click = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t grab = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t scroll = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t alt_click = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t space_drag = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t space_rotate = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t click_modifier_right = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t click_modifier_middle = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t move_mouse = vr::k_ulInvalidActionHandle;
};

class OpenVrInput final {
  public:
    static auto SetActionManifest() -> Result<>;
    static auto Create() -> Result<OpenVrInput>;

    auto Update(vr::ETrackingUniverseOrigin universe) -> void;
    auto UpdateDevices() -> void;
    auto TriggerHaptics(size_t hand, const Haptics& haptics) const -> void;

  private:
    OpenVrInput() = default;

    [[nodiscard]] auto Digital(vr::VRActionHandle_t action, vr::VRInputValueHandle_t source) const -> bool;
    static auto ReadTrackedDevice(vr::TrackedDeviceIndex_t index, TrackedDeviceRole role) -> std::optional<TrackedDevice>;

    OpenVrActions m_actions;
    std::array<OpenVrHandSource, 2> m_hands;
};
