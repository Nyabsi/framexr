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

#include "Config.hpp"
#include "Logger.hpp"
#include "Monado.hpp"
#include "OpenXrInstance.hpp"
#include "OpenXrPose.hpp"
#include "State.hpp"

namespace {

constexpr unsigned char kDefaultProfiles[] = {
#embed "openxr_actions.json"
};

constexpr auto kClickTimes = std::to_array<std::chrono::milliseconds>({
    std::chrono::milliseconds(0),
    std::chrono::milliseconds(500),
    std::chrono::milliseconds(750),
});

constexpr auto
    kProfileFields = std::to_array<std::pair<std::string_view, std::optional<OpenXrActionBinding> OpenXrActionProfile::*>>({
        {"pose", &OpenXrActionProfile::pose},
        {"click", &OpenXrActionProfile::click},
        {"grab", &OpenXrActionProfile::grab},
        {"alt_click", &OpenXrActionProfile::alt_click},
        {"space_drag", &OpenXrActionProfile::space_drag},
        {"space_rotate", &OpenXrActionProfile::space_rotate},
        {"space_reset", &OpenXrActionProfile::space_reset},
        {"click_modifier_right", &OpenXrActionProfile::click_modifier_right},
        {"click_modifier_middle", &OpenXrActionProfile::click_modifier_middle},
        {"move_mouse", &OpenXrActionProfile::move_mouse},
        {"scroll", &OpenXrActionProfile::scroll},
        {"haptic", &OpenXrActionProfile::haptic},
    });

constexpr auto kMonadoRoles = std::to_array<std::pair<MonadoDeviceRole, TrackedDeviceRole>>({
    {MonadoDeviceRole::Head, TrackedDeviceRole::Hmd},
    {MonadoDeviceRole::Eyes, TrackedDeviceRole::None},
    {MonadoDeviceRole::Left, TrackedDeviceRole::LeftHand},
    {MonadoDeviceRole::Right, TrackedDeviceRole::RightHand},
    {MonadoDeviceRole::Gamepad, TrackedDeviceRole::None},
    {MonadoDeviceRole::HandTrackingLeft, TrackedDeviceRole::LeftHand},
    {MonadoDeviceRole::HandTrackingRight, TrackedDeviceRole::RightHand},
});

auto CreateAction(
    const OpenXrInstance& instance,
    XrActionSet action_set,
    std::string_view name,
    std::string_view localized_name,
    XrActionType type
) -> Result<XrAction> {
    XrActionCreateInfo create_info{.type = XR_TYPE_ACTION_CREATE_INFO, .actionType = type};
    name.copy(create_info.actionName, XR_MAX_ACTION_NAME_SIZE - 1);
    localized_name.copy(create_info.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    XrAction action = XR_NULL_HANDLE;
    RETURN_IF_ERROR(instance.Check(xrCreateAction(action_set, &create_info, &action)));
    return action;
}

auto RoleName(TrackedDeviceRole role) -> std::string_view {
    switch (role) {
    case TrackedDeviceRole::None: return "None";
    case TrackedDeviceRole::Hmd: return "Hmd";
    case TrackedDeviceRole::LeftHand: return "LeftHand";
    case TrackedDeviceRole::RightHand: return "RightHand";
    case TrackedDeviceRole::Tracker: return "Tracker";
    }
    return "";
}

auto PushBatteryStatus(Monado& monado, const MonadoDevice& device, TrackedDeviceRole role, AppState& app) -> void {
    const auto status = monado.BatteryStatus(device);
    if (!status || !status->present) {
        return;
    }
    app.input.devices.push_back(TrackedDevice{.soc = status->charge, .charging = status->charging, .role = role});
    LOG_DEBUG("Device {} role {}: {:.0f}% (charging {})", device.index, RoleName(role), status->charge * 100.0f, status->charging);
}

auto IsBoolPath(const std::string& path) -> bool {
    const auto last = std::string_view(path).substr(path.rfind('/') + 1);
    return last == "click" || last == "touch";
}

auto ParseBinding(const nlohmann::json& json) -> Result<OpenXrActionBinding> {
    if (!json.is_object()) {
        return Unexpected("invalid type: expected struct OpenXrActionConfAction");
    }
    OpenXrActionBinding binding;
    for (const auto& [key, member] :
         {std::pair{"left", &OpenXrActionBinding::left}, std::pair{"right", &OpenXrActionBinding::right}}) {
        if (const auto it = json.find(key); it != json.end() && !it->is_null()) {
            if (!it->is_string()) {
                return Unexpected(std::format("invalid type for `{}`: expected a string", key));
            }
            binding.*member = it->get<std::string>();
        }
    }
    if (const auto it = json.find("threshold"); it != json.end() && !it->is_null()) {
        if (!it->is_array() || it->size() != 2 || !(*it)[0].is_number() || !(*it)[1].is_number()) {
            return Unexpected("invalid type for `threshold`: expected an array of 2 numbers");
        }
        binding.threshold = std::array{(*it)[0].get<float>(), (*it)[1].get<float>()};
    }
    for (const auto& [key, member] :
         {std::pair{"double_click", &OpenXrActionBinding::double_click},
          std::pair{"triple_click", &OpenXrActionBinding::triple_click}}) {
        if (const auto it = json.find(key); it != json.end() && !it->is_null()) {
            if (!it->is_boolean()) {
                return Unexpected(std::format("invalid type for `{}`: expected a boolean", key));
            }
            binding.*member = it->get<bool>();
        }
    }
    return binding;
}

} // namespace

auto OpenXrMultiClick::Create(
    const OpenXrInstance& instance,
    XrActionSet action_set,
    std::string_view action_name,
    std::string_view side,
    size_t count
) -> Result<OpenXrMultiClick> {
    auto name = std::format("{}_{}-{}", side, count, action_name);
    const auto float_name = std::format("{}_value", name);
    ASSIGN_OR_RETURN(const auto bool_action, CreateAction(instance, action_set, name, name, XR_ACTION_TYPE_BOOLEAN_INPUT));
    ASSIGN_OR_RETURN(
        const auto float_action,
        CreateAction(instance, action_set, float_name, float_name, XR_ACTION_TYPE_FLOAT_INPUT)
    );
    return OpenXrMultiClick(std::move(name), bool_action, float_action, count);
}

OpenXrMultiClick::OpenXrMultiClick(std::string name, XrAction bool_action, XrAction float_action, size_t count)
    : name_(std::move(name)),
      bool_action_(bool_action),
      float_action_(float_action),
      count_(count),
      previous_(count, std::chrono::steady_clock::now()) {
}

auto OpenXrMultiClick::Check(const OpenXrState& xr, float threshold) -> Result<bool> {
    XrActionStateBoolean bool_state{.type = XR_TYPE_ACTION_STATE_BOOLEAN};
    const XrActionStateGetInfo bool_info{.type = XR_TYPE_ACTION_STATE_GET_INFO, .action = bool_action_};
    RETURN_IF_ERROR(xr.instance->Check(xrGetActionStateBoolean(xr.session, &bool_info, &bool_state)));
    bool state = bool_state.isActive != XR_FALSE && bool_state.currentState != XR_FALSE;

    if (!state) {
        XrActionStateFloat float_state{.type = XR_TYPE_ACTION_STATE_FLOAT};
        const XrActionStateGetInfo float_info{.type = XR_TYPE_ACTION_STATE_GET_INFO, .action = float_action_};
        RETURN_IF_ERROR(xr.instance->Check(xrGetActionStateFloat(xr.session, &float_info, &float_state)));
        state = float_state.isActive != XR_FALSE && float_state.currentState > threshold;
    }

    if (!state) {
        held_active_ = false;
        held_inactive_ = false;
        return false;
    }
    if (held_active_) {
        return true;
    }
    if (held_inactive_) {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    const bool passed = std::ranges::all_of(previous_, [&](const auto& instant) -> bool {
        return now - instant < kClickTimes.at(count_);
    });

    if (passed) {
        LOG_TRACE("{}: passed", name_);
        held_active_ = true;
        held_inactive_ = false;
        std::ranges::fill(previous_, now - std::chrono::seconds(10));
    } else if (count_ > 0) {
        LOG_TRACE("{}: rotate", name_);
        std::ranges::rotate(previous_, previous_.end() - 1);
        previous_.front() = now;
        held_inactive_ = true;
    }
    return passed;
}

auto OpenXrClickAction::Create(const OpenXrInstance& instance, XrActionSet action_set, std::string_view name, std::string_view side)
    -> Result<OpenXrClickAction> {
    std::array<OpenXrMultiClick, 3> handlers;
    for (size_t count = 0; count < handlers.size(); ++count) {
        ASSIGN_OR_RETURN(handlers.at(count), OpenXrMultiClick::Create(instance, action_set, name, side, count));
    }
    return OpenXrClickAction(std::move(handlers));
}

OpenXrClickAction::OpenXrClickAction(std::array<OpenXrMultiClick, 3> handlers) : handlers_(std::move(handlers)) {
}

auto OpenXrClickAction::State(bool before, const OpenXrState& xr, const Config& config) -> Result<bool> {
    const float threshold = before ? config.xr_click_sensitivity_release : config.xr_click_sensitivity;
    for (auto& handler : handlers_) {
        ASSIGN_OR_RETURN(const bool pressed, handler.Check(xr, threshold));
        if (pressed) {
            return true;
        }
    }
    return false;
}

auto OpenXrHandActions::Create(const OpenXrInstance& instance, XrActionSet action_set, std::string_view side)
    -> Result<OpenXrHandActions> {
    OpenXrHandActions actions;
    ASSIGN_OR_RETURN(
        actions.pose,
        CreateAction(instance, action_set, std::format("{}_hand", side), std::format("{} hand pose", side), XR_ACTION_TYPE_POSE_INPUT)
    );
    ASSIGN_OR_RETURN(
        actions.scroll,
        CreateAction(
            instance,
            action_set,
            std::format("{}_scroll", side),
            std::format("{} hand scroll", side),
            XR_ACTION_TYPE_VECTOR2F_INPUT
        )
    );
    ASSIGN_OR_RETURN(
        actions.haptics,
        CreateAction(
            instance,
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

auto OpenXrHand::Update(Pointer& pointer, const OpenXrState& xr, const Config& config) -> Result<> {
    XrSpaceLocation location{.type = XR_TYPE_SPACE_LOCATION};
    RETURN_IF_ERROR(xr.instance->Check(xrLocateSpace(space, xr.stage, xr.predicted_display_time, &location)));

    if ((location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
        const Eigen::Quaternionf current_rotation(Eigen::Matrix3f(pointer.pose.linear()));
        const Eigen::Vector3f current_position = pointer.pose.translation();
        const auto new_rotation = OpenXrPose::ToQuaternion(location.pose.orientation);
        const auto new_position = OpenXrPose::ToVector(location.pose.position);
        const float lerp_factor = std::clamp(1.0f / (xr.fps / 100.0f) * config.pointer_lerp_factor, 0.1f, 1.0f);

        pointer.raw_pose = Eigen::Affine3f::Identity();
        pointer.raw_pose.linear() = new_rotation.toRotationMatrix();
        pointer.raw_pose.translation() = new_position;

        pointer.pose = Eigen::Affine3f::Identity();
        pointer.pose.linear() = OpenXrPose::Lerp(current_rotation, new_rotation, lerp_factor).toRotationMatrix();
        pointer.pose.translation() = current_position * (1.0f - lerp_factor) + new_position * lerp_factor;
    }

    ASSIGN_OR_RETURN(pointer.now.click, actions.click.State(pointer.before.click, xr, config));
    ASSIGN_OR_RETURN(pointer.now.grab, actions.grab.State(pointer.before.grab, xr, config));

    XrActionStateVector2f scroll{.type = XR_TYPE_ACTION_STATE_VECTOR2F};
    const XrActionStateGetInfo scroll_info{.type = XR_TYPE_ACTION_STATE_GET_INFO, .action = actions.scroll};
    RETURN_IF_ERROR(xr.instance->Check(xrGetActionStateVector2f(xr.session, &scroll_info, &scroll)));
    pointer.now.scroll_x = scroll.currentState.x;
    pointer.now.scroll_y = scroll.currentState.y;

    ASSIGN_OR_RETURN(pointer.now.alt_click, actions.alt_click.State(pointer.before.alt_click, xr, config));
    ASSIGN_OR_RETURN(
        pointer.now.click_modifier_right,
        actions.modifier_right.State(pointer.before.click_modifier_right, xr, config)
    );
    ASSIGN_OR_RETURN(
        pointer.now.click_modifier_middle,
        actions.modifier_middle.State(pointer.before.click_modifier_middle, xr, config)
    );
    ASSIGN_OR_RETURN(pointer.now.move_mouse, actions.move_mouse.State(pointer.before.move_mouse, xr, config));
    ASSIGN_OR_RETURN(pointer.now.space_drag, actions.space_drag.State(pointer.before.space_drag, xr, config));
    ASSIGN_OR_RETURN(pointer.now.space_rotate, actions.space_rotate.State(pointer.before.space_rotate, xr, config));
    ASSIGN_OR_RETURN(pointer.now.space_reset, actions.space_reset.State(pointer.before.space_reset, xr, config));
    return {};
}

auto OpenXrInput::Create(const OpenXrState& xr) -> Result<std::unique_ptr<OpenXrInput>> {
    const auto& instance = *xr.instance;

    XrActionSetCreateInfo set_info{.type = XR_TYPE_ACTION_SET_CREATE_INFO, .priority = 0};
    std::string_view("framexr").copy(set_info.actionSetName, XR_MAX_ACTION_SET_NAME_SIZE - 1);
    std::string_view("FrameXR Actions").copy(set_info.localizedActionSetName, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    XrActionSet action_set = XR_NULL_HANDLE;
    RETURN_IF_ERROR(instance.Check(xrCreateActionSet(instance.Handle(), &set_info, &action_set)));

    ASSIGN_OR_RETURN(auto left, OpenXrHandActions::Create(instance, action_set, "left"));
    ASSIGN_OR_RETURN(auto right, OpenXrHandActions::Create(instance, action_set, "right"));
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
        hands.at(i).actions = std::move(actions.at(i));
        const XrActionSpaceCreateInfo space_info{
            .type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
            .action = hands.at(i).actions.pose,
            .subactionPath = XR_NULL_PATH,
            .poseInActionSpace = {.orientation = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f}, .position = {}},
        };
        RETURN_IF_ERROR(instance.Check(xrCreateActionSpace(xr.session, &space_info, &hands.at(i).space)));
    }
    return std::unique_ptr<OpenXrInput>(new OpenXrInput(action_set, std::move(hands)));
}

OpenXrInput::OpenXrInput(XrActionSet action_set, std::array<OpenXrHand, 2> hands)
    : action_set_(action_set),
      hands_(std::move(hands)) {
}

OpenXrInput::~OpenXrInput() {
    for (const auto& hand : hands_) {
        if (hand.space != XR_NULL_HANDLE) {
            xrDestroySpace(hand.space);
        }
    }
    xrDestroyActionSet(action_set_);
}

auto OpenXrInput::Update(const OpenXrState& xr, AppState& app) -> Result<> {
    const XrActiveActionSet active_set{.actionSet = action_set_, .subactionPath = XR_NULL_PATH};
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
        app.input.hmd.linear() = hmd.linear();
    }
    if ((location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0) {
        app.input.hmd.translation() = hmd.translation();
    }

    for (size_t i = 0; i < hands_.size(); ++i) {
        RETURN_IF_ERROR(hands_.at(i).Update(app.input.pointers.at(i), xr, app.config));
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
    const XrHapticActionInfo action_info{.type = XR_TYPE_HAPTIC_ACTION_INFO, .action = hands_.at(hand).actions.haptics};
    xrApplyHapticFeedback(xr.session, &action_info, reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
}

auto OpenXrInput::UpdateDevices(AppState& app, Monado& monado) -> void {
    app.input.devices.clear();

    std::vector<uint32_t> seen;
    for (const auto& [monado_role, role] : kMonadoRoles) {
        const auto device = monado.DeviceFromRole(monado_role);
        if (device && std::ranges::find(seen, device->index) == seen.end()) {
            seen.push_back(device->index);
            PushBatteryStatus(monado, *device, role, app);
        }
    }
    if (const auto devices = monado.Devices()) {
        for (const auto& device : *devices) {
            if (std::ranges::find(seen, device.index) != seen.end()) {
                continue;
            }
            const auto role = device.name_id >= 4 && device.name_id <= 8 ? TrackedDeviceRole::Tracker : TrackedDeviceRole::None;
            PushBatteryStatus(monado, device, role, app);
        }
    }
    app.input.SortDevices();
}

auto OpenXrInput::ParseProfiles(const nlohmann::json& json) -> Result<std::vector<OpenXrActionProfile>> {
    if (!json.is_array()) {
        return Unexpected("invalid type: expected a sequence");
    }
    std::vector<OpenXrActionProfile> profiles;
    for (const auto& entry : json) {
        if (!entry.is_object()) {
            return Unexpected("invalid type: expected struct OpenXrActionConfProfile");
        }
        const auto profile = entry.find("profile");
        if (profile == entry.end() || !profile->is_string()) {
            return Unexpected("missing field `profile`");
        }
        OpenXrActionProfile parsed{.profile = profile->get<std::string>()};
        for (const auto& [key, member] : kProfileFields) {
            if (const auto it = entry.find(key); it != entry.end() && !it->is_null()) {
                ASSIGN_OR_RETURN(parsed.*member, ParseBinding(*it));
            }
        }
        profiles.push_back(std::move(parsed));
    }
    return profiles;
}

auto OpenXrInput::LoadProfiles() -> std::vector<OpenXrActionProfile> {
    const std::string_view defaults(reinterpret_cast<const char*>(kDefaultProfiles), sizeof(kDefaultProfiles));
    auto profiles = ParseProfiles(nlohmann::json::parse(defaults, nullptr, false, true));
    if (!profiles) {
        FatalError(std::format("Invalid built-in openxr_actions.json: {}", profiles.error().Message()));
    }

    const auto override_text = Config::LoadText("openxr_actions.json");
    if (!override_text) {
        return *profiles;
    }
    const auto json = nlohmann::json::parse(*override_text, nullptr, false, true);
    auto overrides = json.is_discarded() ? Result<std::vector<OpenXrActionProfile>>(Unexpected("invalid JSON"))
                                         : ParseProfiles(json);
    if (!overrides) {
        LOG_ERROR("Failed to load openxr_actions.json: {}", overrides.error().Message());
        return *profiles;
    }
    for (auto& replacement : *overrides) {
        const auto existing = std::ranges::find(*profiles, replacement.profile, &OpenXrActionProfile::profile);
        if (existing != profiles->end()) {
            *existing = std::move(replacement);
        }
    }
    return *profiles;
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

    const auto custom = std::to_array<
        std::pair<std::optional<OpenXrActionBinding> OpenXrActionProfile::*, OpenXrClickAction OpenXrHandActions::*>>({
        {&OpenXrActionProfile::click, &OpenXrHandActions::click},
        {&OpenXrActionProfile::alt_click, &OpenXrHandActions::alt_click},
        {&OpenXrActionProfile::grab, &OpenXrHandActions::grab},
        {&OpenXrActionProfile::space_drag, &OpenXrHandActions::space_drag},
        {&OpenXrActionProfile::space_rotate, &OpenXrHandActions::space_rotate},
        {&OpenXrActionProfile::space_reset, &OpenXrHandActions::space_reset},
        {&OpenXrActionProfile::click_modifier_right, &OpenXrHandActions::modifier_right},
        {&OpenXrActionProfile::click_modifier_middle, &OpenXrHandActions::modifier_middle},
        {&OpenXrActionProfile::move_mouse, &OpenXrHandActions::move_mouse},
    });

    for (const auto& profile : LoadProfiles()) {
        XrPath profile_path = XR_NULL_PATH;
        if (XR_FAILED(xrStringToPath(instance.Handle(), profile.profile.c_str(), &profile_path))) {
            LOG_DEBUG("Profile not supported: {}", profile.profile);
            continue;
        }

        std::vector<XrActionSuggestedBinding> bindings;
        const auto bind_simple = [&](const std::optional<OpenXrActionBinding>& binding,
                                     XrAction OpenXrHandActions::* action) -> void {
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
