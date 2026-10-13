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

#include "OpenXrInput.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <utility>

#include "Bindings.hpp"
#include "Config.hpp"
#include "Devices.hpp"
#include "Interaction.hpp"
#include "Logger.hpp"
#include "Monado.hpp"
#include "OpenXrInstance.hpp"
#include "OpenXrPose.hpp"

namespace {

constexpr auto kMonadoRoles = std::to_array<std::pair<MonadoDeviceRole, TrackedDeviceRole>>({
    {MonadoDeviceRole::Head, TrackedDeviceRole::Hmd},
    {MonadoDeviceRole::Eyes, TrackedDeviceRole::None},
    {MonadoDeviceRole::Left, TrackedDeviceRole::LeftHand},
    {MonadoDeviceRole::Right, TrackedDeviceRole::RightHand},
    {MonadoDeviceRole::Gamepad, TrackedDeviceRole::None},
    {MonadoDeviceRole::HandTrackingLeft, TrackedDeviceRole::LeftHand},
    {MonadoDeviceRole::HandTrackingRight, TrackedDeviceRole::RightHand},
});

auto ReadDevice(Monado& monado, const MonadoDevice& device, TrackedDeviceRole role) -> TrackedDevice {
    TrackedDevice tracked{.id = device.index, .role = role};
    if (const auto status = monado.BatteryStatus(device); status && status->present) {
        tracked.soc = status->charge;
        tracked.charging = status->charging;
    }
    return tracked;
}

auto IsBoolPath(const std::string& path) -> bool {
    const auto last = std::string_view(path).substr(path.rfind('/') + 1);
    return last == "click" || last == "touch";
}

} // namespace

auto OpenXrInput::CreateHandActions(const OpenXrInstance& instance, XrActionSet action_set, std::string_view side)
    -> Result<OpenXrHandActions> {
    OpenXrHandActions actions;
    ASSIGN_OR_RETURN(
        actions.pose,
        instance.CreateAction(action_set, std::format("{}_hand", side), std::format("{} hand pose", side), XR_ACTION_TYPE_POSE_INPUT)
    );
    ASSIGN_OR_RETURN(
        actions.scroll,
        instance.CreateAction(
            action_set,
            std::format("{}_scroll", side),
            std::format("{} hand scroll", side),
            XR_ACTION_TYPE_VECTOR2F_INPUT
        )
    );
    ASSIGN_OR_RETURN(
        actions.haptics,
        instance.CreateAction(
            action_set,
            std::format("{}_haptics", side),
            std::format("{} hand haptics", side),
            XR_ACTION_TYPE_VIBRATION_OUTPUT
        )
    );

    const auto clicks = std::to_array<std::pair<std::string_view, OpenXrClickAction OpenXrHandActions::*>>({
        {"click", &OpenXrHandActions::click},
        {"grab", &OpenXrHandActions::grab},
        {"alt_click", &OpenXrHandActions::alt_click},
        {"space_drag", &OpenXrHandActions::space_drag},
        {"space_rotate", &OpenXrHandActions::space_rotate},
        {"space_reset", &OpenXrHandActions::space_reset},
        {"click_modifier_right", &OpenXrHandActions::modifier_right},
        {"click_modifier_middle", &OpenXrHandActions::modifier_middle},
        {"move_mouse", &OpenXrHandActions::move_mouse},
    });
    for (const auto& [name, member] : clicks) {
        ASSIGN_OR_RETURN(actions.*member, OpenXrClickAction::Create(instance, action_set, name, side));
    }
    return actions;
}

auto OpenXrInput::Create(const OpenXrState& xr) -> Result<std::unique_ptr<OpenXrInput>> {
    const auto& instance = *xr.instance;

    XrActionSetCreateInfo set_info{.type = XR_TYPE_ACTION_SET_CREATE_INFO, .priority = 0};
    std::string_view("framexr").copy(set_info.actionSetName, XR_MAX_ACTION_SET_NAME_SIZE - 1);
    std::string_view("FrameXR Actions").copy(set_info.localizedActionSetName, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    XrActionSet action_set = XR_NULL_HANDLE;
    RETURN_IF_ERROR(instance.Check(xrCreateActionSet(instance.Handle(), &set_info, &action_set)));

    ASSIGN_OR_RETURN(auto left, CreateHandActions(instance, action_set, "left"));
    ASSIGN_OR_RETURN(auto right, CreateHandActions(instance, action_set, "right"));
    std::array<OpenXrHandActions, 2> actions{std::move(left), std::move(right)};

    SuggestBindings(instance, actions);

    const XrSessionActionSetsAttachInfo attach_info{
        .type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
        .countActionSets = 1,
        .actionSets = &action_set,
    };
    RETURN_IF_ERROR(instance.Check(xrAttachSessionActionSets(xr.session, &attach_info)));

    std::array<OpenXrHand, 2> hands;
    for (size_t i = 0; i < hands.size(); ++i) {
        hands.at(i).SetActions(std::move(actions.at(i)));
        const XrActionSpaceCreateInfo space_info{
            .type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
            .action = hands.at(i).Actions().pose,
            .subactionPath = XR_NULL_PATH,
            .poseInActionSpace = {.orientation = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f}, .position = {}},
        };
        RETURN_IF_ERROR(instance.Check(xrCreateActionSpace(xr.session, &space_info, &hands.at(i).Space())));
    }
    return std::unique_ptr<OpenXrInput>(new OpenXrInput(action_set, std::move(hands)));
}

OpenXrInput::OpenXrInput(XrActionSet action_set, std::array<OpenXrHand, 2> hands)
    : m_action_set(action_set),
      m_hands(std::move(hands)) {
}

OpenXrInput::~OpenXrInput() {
    for (const auto& hand : m_hands) {
        if (hand.Space() != XR_NULL_HANDLE) {
            xrDestroySpace(hand.Space());
        }
    }
    xrDestroyActionSet(m_action_set);
}

auto OpenXrInput::Update(const OpenXrState& xr) -> Result<> {
    const XrActiveActionSet active_set{.actionSet = m_action_set, .subactionPath = XR_NULL_PATH};
    const XrActionsSyncInfo sync_info{
        .type = XR_TYPE_ACTIONS_SYNC_INFO,
        .countActiveActionSets = 1,
        .activeActionSets = &active_set
    };
    RETURN_IF_ERROR(xr.instance->Check(xrSyncActions(xr.session, &sync_info)));

    XrSpaceLocation location{.type = XR_TYPE_SPACE_LOCATION};
    RETURN_IF_ERROR(xr.instance->Check(xrLocateSpace(xr.view, xr.stage, xr.predicted_display_time, &location)));
    const auto hmd = OpenXrPose::ToTransform(location.pose);
    if ((location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
        g_interaction->Hmd().linear() = hmd.linear();
    }
    if ((location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0) {
        g_interaction->Hmd().translation() = hmd.translation();
    }

    for (size_t i = 0; i < m_hands.size(); ++i) {
        RETURN_IF_ERROR(m_hands.at(i).Update(g_interaction->Pointers().at(i), xr));
    }
    return {};
}

auto OpenXrInput::ApplyHaptics(const OpenXrState& xr, size_t hand, const Haptics& haptics) -> void {
    const XrHapticVibration vibration{
        .type = XR_TYPE_HAPTIC_VIBRATION,
        .duration = static_cast<XrDuration>(static_cast<double>(haptics.duration) * 1'000'000'000.0),
        .frequency = haptics.frequency,
        .amplitude = haptics.intensity,
    };
    const XrHapticActionInfo action_info{.type = XR_TYPE_HAPTIC_ACTION_INFO, .action = m_hands.at(hand).Actions().haptics};
    xrApplyHapticFeedback(xr.session, &action_info, reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
}

auto OpenXrInput::UpdateDevices(Monado& monado) -> void {
    std::vector<TrackedDevice> tracked;
    std::vector<uint32_t> seen;
    for (const auto& [monado_role, role] : kMonadoRoles) {
        const auto device = monado.DeviceFromRole(monado_role);
        if (device && std::ranges::find(seen, device->index) == seen.end()) {
            seen.push_back(device->index);
            tracked.push_back(ReadDevice(monado, *device, role));
        }
    }
    if (const auto devices = monado.Devices()) {
        for (const auto& device : *devices) {
            if (std::ranges::find(seen, device.index) != seen.end()) {
                continue;
            }
            const auto role = device.name_id >= 4 && device.name_id <= 8 ? TrackedDeviceRole::Tracker : TrackedDeviceRole::None;
            tracked.push_back(ReadDevice(monado, device, role));
        }
    }
    g_devices->Update(std::move(tracked));
}

auto OpenXrInput::SuggestBindings(const OpenXrInstance& instance, const std::array<OpenXrHandActions, 2>& hands) -> void {
    const auto to_path = [&](const std::optional<std::string>& path) -> std::optional<XrPath> {
        if (!path) {
            return std::nullopt;
        }
        XrPath result = XR_NULL_PATH;
        if (XR_FAILED(xrStringToPath(instance.Handle(), path->c_str(), &result))) {
            LOG_WARN("Invalid binding path: {}", *path);
            return std::nullopt;
        }
        return result;
    };

    const auto
        custom = std::to_array<std::pair<std::optional<InputBinding> BindingProfile::*, OpenXrClickAction OpenXrHandActions::*>>({
            {&BindingProfile::click, &OpenXrHandActions::click},
            {&BindingProfile::alt_click, &OpenXrHandActions::alt_click},
            {&BindingProfile::grab, &OpenXrHandActions::grab},
            {&BindingProfile::space_drag, &OpenXrHandActions::space_drag},
            {&BindingProfile::space_rotate, &OpenXrHandActions::space_rotate},
            {&BindingProfile::space_reset, &OpenXrHandActions::space_reset},
            {&BindingProfile::click_modifier_right, &OpenXrHandActions::modifier_right},
            {&BindingProfile::click_modifier_middle, &OpenXrHandActions::modifier_middle},
            {&BindingProfile::move_mouse, &OpenXrHandActions::move_mouse},
        });

    for (const auto& profile : Bindings::Load()) {
        XrPath profile_path = XR_NULL_PATH;
        if (XR_FAILED(xrStringToPath(instance.Handle(), profile.profile.c_str(), &profile_path))) {
            LOG_DEBUG("Profile not supported: {}", profile.profile);
            continue;
        }

        std::vector<XrActionSuggestedBinding> bindings;
        const auto bind_simple = [&](const std::optional<InputBinding>& binding, XrAction OpenXrHandActions::* action) -> void {
            if (!binding) {
                return;
            }
            if (const auto path = to_path(binding->left)) {
                bindings.push_back({.action = hands[0].*action, .binding = *path});
            }
            if (const auto path = to_path(binding->right)) {
                bindings.push_back({.action = hands[1].*action, .binding = *path});
            }
        };
        bind_simple(profile.pose, &OpenXrHandActions::pose);
        bind_simple(profile.haptic, &OpenXrHandActions::haptics);
        bind_simple(profile.scroll, &OpenXrHandActions::scroll);

        for (const auto& [binding_member, click_member] : custom) {
            const auto& binding = profile.*binding_member;
            if (!binding) {
                continue;
            }
            const size_t count = binding->triple_click.value_or(false) ? 2 : (binding->double_click.value_or(false) ? 1 : 0);
            for (const auto& [side_path, hand] : {std::pair{&binding->left, &hands[0]}, std::pair{&binding->right, &hands[1]}}) {
                const auto path = to_path(*side_path);
                if (!path) {
                    continue;
                }
                const auto& handler = ((*hand).*click_member).Handler(count);
                bindings.push_back(
                    {.action = IsBoolPath(**side_path) ? handler.BoolAction() : handler.FloatAction(), .binding = *path}
                );
            }
        }

        const XrInteractionProfileSuggestedBinding suggested{
            .type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
            .interactionProfile = profile_path,
            .countSuggestedBindings = static_cast<uint32_t>(bindings.size()),
            .suggestedBindings = bindings.data(),
        };
        if (XR_FAILED(xrSuggestInteractionProfileBindings(instance.Handle(), &suggested))) {
            LOG_ERROR("Bad bindings for {}", profile.profile.size() > 22 ? profile.profile.substr(22) : profile.profile);
            LOG_ERROR("Verify config: ~/.config/framexr/openxr_actions.json");
        }
    }
}
