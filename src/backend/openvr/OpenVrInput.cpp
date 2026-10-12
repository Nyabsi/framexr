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

#include "OpenVrInput.hpp"

#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <string_view>

#include "Config.hpp"
#include "Input.hpp"
#include "Logger.hpp"
#include "OpenVrMath.hpp"

namespace {

constexpr unsigned char kActions[] = {
#embed "actions.json"
};
constexpr unsigned char kBindingKnuckles[] = {
#embed "actions_binding_knuckles.json"
};
constexpr unsigned char kBindingVive[] = {
#embed "actions_binding_vive.json"
};
constexpr unsigned char kBindingOculus[] = {
#embed "actions_binding_oculus.json"
};

constexpr std::array<const char*, 2> kInputSources{"/user/hand/left", "/user/hand/right"};
constexpr std::array<const char*, 2> kPosePaths{"/actions/default/in/LeftHand", "/actions/default/in/RightHand"};
constexpr std::array<const char*, 2> kHapticsPaths{"/actions/default/out/HapticsLeft", "/actions/default/out/HapticsRight"};

auto AsText(std::span<const unsigned char> bytes) -> std::string_view {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

auto WriteFile(const std::filesystem::path& path, std::string_view contents) -> Result<> {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return Unexpected(std::format("Could not create {}", path.string()));
    }
    stream << contents;
    if (!stream) {
        return Unexpected(std::format("Could not write {}", path.string()));
    }
    return {};
}

auto InputError(vr::EVRInputError error) -> Error {
    return Error(std::format("ovr input error: {}", static_cast<int>(error)));
}

auto ActionHandle(const char* path) -> Result<vr::VRActionHandle_t> {
    vr::VRActionHandle_t handle = vr::k_ulInvalidActionHandle;
    if (const auto error = vr::VRInput()->GetActionHandle(path, &handle); error != vr::VRInputError_None) {
        return std::unexpected(InputError(error));
    }
    return handle;
}

auto SourceHandle(const char* path) -> Result<vr::VRInputValueHandle_t> {
    vr::VRInputValueHandle_t handle = vr::k_ulInvalidInputValueHandle;
    if (const auto error = vr::VRInput()->GetInputSourceHandle(path, &handle); error != vr::VRInputError_None) {
        return std::unexpected(InputError(error));
    }
    return handle;
}

} // namespace

auto OpenVrInput::SetActionManifest() -> Result<> {
    const auto action_path = ConfigStore::Root() / "actions.json";
    if (auto written = WriteFile(action_path, AsText(kActions)); !written) {
        LOG_WARN("Could not write action manifest: {}", written.error().Message());
    }

    const auto bindings = std::to_array<std::pair<const char*, std::string_view>>({
        {"actions_binding_knuckles.json", AsText(kBindingKnuckles)},
        {"actions_binding_vive.json", AsText(kBindingVive)},
        {"actions_binding_oculus.json", AsText(kBindingOculus)},
    });
    for (const auto& [name, contents] : bindings) {
        const auto path = ConfigStore::Root() / name;
        if (!std::filesystem::is_regular_file(path)) {
            RETURN_IF_ERROR(WriteFile(path, contents));
        }
    }

    if (const auto error = vr::VRInput()->SetActionManifestPath(action_path.string().c_str()); error != vr::VRInputError_None) {
        return Unexpected(std::format("Failed to set action manifest: {}", InputError(error).Message()));
    }
    return {};
}

auto OpenVrInput::Create() -> Result<OpenVrInput> {
    OpenVrInput input;
    auto& actions = input.m_actions;
    if (const auto error = vr::VRInput()->GetActionSetHandle("/actions/default", &actions.set); error != vr::VRInputError_None) {
        return std::unexpected(InputError(error));
    }
    ASSIGN_OR_RETURN(actions.click, ActionHandle("/actions/default/in/Click"));
    ASSIGN_OR_RETURN(actions.grab, ActionHandle("/actions/default/in/Grab"));
    ASSIGN_OR_RETURN(actions.scroll, ActionHandle("/actions/default/in/Scroll"));
    ASSIGN_OR_RETURN(actions.alt_click, ActionHandle("/actions/default/in/AltClick"));
    ASSIGN_OR_RETURN(actions.space_drag, ActionHandle("/actions/default/in/SpaceDrag"));
    ASSIGN_OR_RETURN(actions.space_rotate, ActionHandle("/actions/default/in/SpaceRotate"));
    ASSIGN_OR_RETURN(actions.click_modifier_right, ActionHandle("/actions/default/in/ClickModifierRight"));
    ASSIGN_OR_RETURN(actions.click_modifier_middle, ActionHandle("/actions/default/in/ClickModifierMiddle"));
    ASSIGN_OR_RETURN(actions.move_mouse, ActionHandle("/actions/default/in/MoveMouse"));

    for (size_t i = 0; i < input.m_hands.size(); ++i) {
        ASSIGN_OR_RETURN(input.m_hands[i].input, SourceHandle(kInputSources[i]));
    }
    for (size_t i = 0; i < input.m_hands.size(); ++i) {
        ASSIGN_OR_RETURN(input.m_hands[i].pose, ActionHandle(kPosePaths[i]));
    }
    for (size_t i = 0; i < input.m_hands.size(); ++i) {
        ASSIGN_OR_RETURN(input.m_hands[i].haptics, ActionHandle(kHapticsPaths[i]));
    }
    return input;
}

auto OpenVrInput::TriggerHaptics(size_t hand, const Haptics& haptics) const -> void {
    vr::VRInput()->TriggerHapticVibrationAction(
        m_hands[hand].haptics,
        0.0f,
        haptics.duration,
        haptics.frequency,
        haptics.intensity,
        vr::k_ulInvalidInputValueHandle
    );
}

auto OpenVrInput::Digital(vr::VRActionHandle_t action, vr::VRInputValueHandle_t source) const -> bool {
    vr::InputDigitalActionData_t data{};
    if (vr::VRInput()->GetDigitalActionData(action, &data, sizeof(data), source) != vr::VRInputError_None) {
        return false;
    }
    return data.bState;
}

auto OpenVrInput::Update(vr::ETrackingUniverseOrigin universe) -> void {
    vr::VRActiveActionSet_t active_set{.ulActionSet = m_actions.set};
    vr::VRInput()->UpdateActionState(&active_set, sizeof(active_set), 1);

    std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> poses;
    vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(universe, 0.005f, poses.data(), static_cast<uint32_t>(poses.size()));
    g_input->SetHmd(OpenVrMath::ToAffine(poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking));

    for (size_t i = 0; i < m_hands.size(); ++i) {
        const auto& hand = m_hands[i];
        auto& pointer = g_input->Pointers()[i];

        if (hand.device) {
            pointer.SetRawPose(OpenVrMath::ToAffine(poses[*hand.device].mDeviceToAbsoluteTracking));
        }

        vr::InputPoseActionData_t pose;
        if (vr::VRInput()
                ->GetPoseActionDataRelativeToNow(hand.pose, universe, 0.005f, &pose, sizeof(pose), vr::k_ulInvalidInputValueHandle)
            == vr::VRInputError_None) {
            pointer.SetPose(OpenVrMath::ToAffine(pose.pose.mDeviceToAbsoluteTracking));
        }

        pointer.Now().click = Digital(m_actions.click, hand.input);
        pointer.Now().grab = Digital(m_actions.grab, hand.input);
        pointer.Now().alt_click = Digital(m_actions.alt_click, hand.input);
        pointer.Now().space_drag = Digital(m_actions.space_drag, hand.input);
        pointer.Now().space_rotate = Digital(m_actions.space_rotate, hand.input);
        pointer.Now().click_modifier_right = Digital(m_actions.click_modifier_right, hand.input);
        pointer.Now().click_modifier_middle = Digital(m_actions.click_modifier_middle, hand.input);
        pointer.Now().move_mouse = Digital(m_actions.move_mouse, hand.input);

        vr::InputAnalogActionData_t scroll{};
        if (vr::VRInput()->GetAnalogActionData(m_actions.scroll, &scroll, sizeof(scroll), hand.input) != vr::VRInputError_None) {
            scroll.x = 0.0f;
            scroll.y = 0.0f;
        }
        pointer.Now().scroll_x = scroll.x;
        pointer.Now().scroll_y = scroll.y;
    }
}

auto OpenVrInput::ReadTrackedDevice(vr::TrackedDeviceIndex_t index, TrackedDeviceRole role) -> std::optional<TrackedDevice> {
    auto* system = vr::VRSystem();
    vr::ETrackedPropertyError error = vr::TrackedProp_Success;

    TrackedDevice device{.role = role};
    const float soc = system->GetFloatTrackedDeviceProperty(index, vr::Prop_DeviceBatteryPercentage_Float, &error);
    if (error == vr::TrackedProp_Success) {
        device.soc = soc;
        const bool charging = system->GetBoolTrackedDeviceProperty(index, vr::Prop_DeviceIsCharging_Bool, &error);
        device.charging = error == vr::TrackedProp_Success && charging;
    }

    std::array<char, vr::k_unMaxPropertyStringSize> system_name{};
    system->GetStringTrackedDeviceProperty(
        index,
        vr::Prop_TrackingSystemName_String,
        system_name.data(),
        static_cast<uint32_t>(system_name.size()),
        &error
    );
    if (error == vr::TrackedProp_Success && std::string_view(system_name.data()).contains("ALVR")) {
        return std::nullopt;
    }
    return device;
}

auto OpenVrInput::UpdateDevices() -> void {
    auto* system = vr::VRSystem();
    g_input->Devices().clear();
    for (vr::TrackedDeviceIndex_t index = 0; index < vr::k_unMaxTrackedDeviceCount; ++index) {
        if (!system->IsTrackedDeviceConnected(index)) {
            continue;
        }

        TrackedDeviceRole role = TrackedDeviceRole::None;
        switch (system->GetTrackedDeviceClass(index)) {
        case vr::TrackedDeviceClass_HMD: role = TrackedDeviceRole::Hmd; break;
        case vr::TrackedDeviceClass_Controller:
            switch (system->GetControllerRoleForTrackedDeviceIndex(index)) {
            case vr::TrackedControllerRole_LeftHand:
                m_hands[0].device = index;
                role = TrackedDeviceRole::LeftHand;
                break;
            case vr::TrackedControllerRole_RightHand:
                m_hands[1].device = index;
                role = TrackedDeviceRole::RightHand;
                break;
            default: continue;
            }
            break;
        case vr::TrackedDeviceClass_GenericTracker: role = TrackedDeviceRole::Tracker; break;
        default: continue;
        }

        if (auto device = ReadTrackedDevice(index, role)) {
            g_input->Devices().push_back(*device);
        }
    }
    g_input->SortDevices();
}
