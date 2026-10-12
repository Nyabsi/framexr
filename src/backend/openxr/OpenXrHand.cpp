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

#include "OpenXrHand.hpp"

#include <algorithm>

#include "Config.hpp"
#include "Input.hpp"
#include "OpenXrInstance.hpp"
#include "OpenXrPose.hpp"

auto OpenXrHand::Update(Pointer& pointer, const OpenXrState& xr) -> Result<> {
    XrSpaceLocation location{.type = XR_TYPE_SPACE_LOCATION};
    RETURN_IF_ERROR(xr.instance->Check(xrLocateSpace(m_space, xr.stage, xr.predicted_display_time, &location)));

    if ((location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
        const Eigen::Quaternionf current_rotation(Eigen::Matrix3f(pointer.Pose().linear()));
        const Eigen::Vector3f current_position = pointer.Pose().translation();
        const auto new_rotation = OpenXrPose::ToQuaternion(location.pose.orientation);
        const auto new_position = OpenXrPose::ToVector(location.pose.position);
        const float lerp_factor = std::clamp(1.0f / (xr.fps / 100.0f) * g_config->pointer_lerp_factor, 0.1f, 1.0f);

        pointer.SetRawPose(Eigen::Affine3f::Identity());
        pointer.RawPose().linear() = new_rotation.toRotationMatrix();
        pointer.RawPose().translation() = new_position;

        pointer.SetPose(Eigen::Affine3f::Identity());
        pointer.Pose().linear() = OpenXrPose::Lerp(current_rotation, new_rotation, lerp_factor).toRotationMatrix();
        pointer.Pose().translation() = current_position * (1.0f - lerp_factor) + new_position * lerp_factor;
    }

    ASSIGN_OR_RETURN(pointer.Now().click, m_actions.click.State(pointer.Before().click, xr));
    ASSIGN_OR_RETURN(pointer.Now().grab, m_actions.grab.State(pointer.Before().grab, xr));

    XrActionStateVector2f scroll{.type = XR_TYPE_ACTION_STATE_VECTOR2F};
    const XrActionStateGetInfo scroll_info{.type = XR_TYPE_ACTION_STATE_GET_INFO, .action = m_actions.scroll};
    RETURN_IF_ERROR(xr.instance->Check(xrGetActionStateVector2f(xr.session, &scroll_info, &scroll)));
    pointer.Now().scroll_x = scroll.currentState.x;
    pointer.Now().scroll_y = scroll.currentState.y;

    ASSIGN_OR_RETURN(pointer.Now().alt_click, m_actions.alt_click.State(pointer.Before().alt_click, xr));
    ASSIGN_OR_RETURN(pointer.Now().click_modifier_right, m_actions.modifier_right.State(pointer.Before().click_modifier_right, xr));
    ASSIGN_OR_RETURN(
        pointer.Now().click_modifier_middle,
        m_actions.modifier_middle.State(pointer.Before().click_modifier_middle, xr)
    );
    ASSIGN_OR_RETURN(pointer.Now().move_mouse, m_actions.move_mouse.State(pointer.Before().move_mouse, xr));
    ASSIGN_OR_RETURN(pointer.Now().space_drag, m_actions.space_drag.State(pointer.Before().space_drag, xr));
    ASSIGN_OR_RETURN(pointer.Now().space_rotate, m_actions.space_rotate.State(pointer.Before().space_rotate, xr));
    ASSIGN_OR_RETURN(pointer.Now().space_reset, m_actions.space_reset.State(pointer.Before().space_reset, xr));
    return {};
}
