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

#include "Button.hpp"

#include <array>
#include <format>
#include <numbers>
#include <utility>

#include "CustomUi.hpp"
#include "Hid.hpp"
#include "Json.hpp"
#include "Logger.hpp"
#include "State.hpp"
#include "Watch.hpp"
#include "framexr_config.h"

#if FRAMEXR_FEATURE_WAYLAND
#include "Mirror.hpp"
#endif

namespace {

constexpr unsigned char kThumpSound[] = {
#embed "380885.wav"
};

constexpr std::array<std::string_view, 2> kEnabledDisabled{"enabled", "disabled"};

template <typename Enum, size_t Count>
auto ParseEnum(const nlohmann::json& json, const std::array<std::pair<Enum, std::string_view>, Count>& names) -> Enum {
    const auto name = Json::VariantName(json);
    for (const auto& [value, value_name] : names) {
        if (value_name == name) {
            return value;
        }
    }
    throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}`", name), &json);
}

constexpr auto kSystemActions = std::to_array<std::pair<SystemAction, std::string_view>>({
    {SystemAction::ToggleAllowSliding, "ToggleAllowSliding"},
    {SystemAction::Recenter, "Recenter"},
    {SystemAction::ToggleNotificationSounds, "ToggleNotificationSounds"},
    {SystemAction::ToggleNotifications, "ToggleNotifications"},
    {SystemAction::ToggleRotateLock, "ToggleRotateLock"},
    {SystemAction::PlayspaceResetOffset, "PlayspaceResetOffset"},
    {SystemAction::PlayspaceFixFloor, "PlayspaceFixFloor"},
    {SystemAction::RecalculateExtent, "RecalculateExtent"},
    {SystemAction::PersistConfig, "PersistConfig"},
    {SystemAction::PersistLayout, "PersistLayout"},
});

constexpr auto kHighlightTests = std::to_array<std::pair<HighlightTest, std::string_view>>({
    {HighlightTest::AllowSliding, "AllowSliding"},
    {HighlightTest::NotificationSounds, "NotificationSounds"},
    {HighlightTest::Notifications, "Notifications"},
    {HighlightTest::RotateLock, "RorateLock"},
});

constexpr auto kWindowActions = std::to_array<std::pair<WindowAction, std::string_view>>({
    {WindowAction::ShowMirror, "ShowMirror"},
    {WindowAction::ShowUi, "ShowUi"},
    {WindowAction::Destroy, "Destroy"},
});

constexpr auto kColorChannels = std::to_array<std::pair<ColorChannel, std::string_view>>({
    {ColorChannel::R, "R"},
    {ColorChannel::G, "G"},
    {ColorChannel::B, "B"},
    {ColorChannel::All, "All"},
});

constexpr auto kAxes = std::to_array<std::pair<Axis, std::string_view>>({
    {Axis::X, "X"},
    {Axis::Y, "Y"},
    {Axis::Z, "Z"},
});

constexpr auto kViewAngleKinds = std::to_array<std::pair<ViewAngleKind, std::string_view>>({
    {ViewAngleKind::MinOpacity, "MinOpacity"},
    {ViewAngleKind::MaxOpacity, "MaxOpacity"},
});

constexpr auto kPressRelease = std::to_array<std::pair<PressRelease, std::string_view>>({
    {PressRelease::Release, "Release"},
    {PressRelease::Press, "Press"},
});

auto ParseWatchAction(const nlohmann::json& json) -> WatchAction {
    const auto name = Json::VariantName(json);
    const auto& payload = Json::VariantPayload(json);
    if (name == "Hide") {
        return WatchHide{};
    }
    if (name == "SwitchHands") {
        return WatchSwitchHands{};
    }
    if (name == "ViewAngle") {
        return WatchViewAngle{.kind = ParseEnum(payload.at("kind"), kViewAngleKinds), .delta = payload.at("delta").get<float>()};
    }
    if (name == "Rotation") {
        return WatchRotation{.axis = ParseEnum(payload.at("axis"), kAxes), .delta = payload.at("delta").get<float>()};
    }
    if (name == "Position") {
        return WatchPosition{.axis = ParseEnum(payload.at("axis"), kAxes), .delta = payload.at("delta").get<float>()};
    }
    throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for WatchAction", name), &json);
}

auto ParseOscValue(const nlohmann::json& json) -> OscValue {
    const auto type = json.at("type").get<std::string>();
    const auto& value = json.at("value");
    if (type == "Int") {
        return value.get<int32_t>();
    }
    if (type == "Float") {
        return value.get<float>();
    }
    if (type == "String") {
        return value.get<std::string>();
    }
    if (type == "Bool") {
        return value.get<bool>();
    }
    throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for OscValue", type), &json);
}

auto OptionalActions(const nlohmann::json& json, std::string_view key) -> std::optional<std::vector<ButtonAction>> {
    const auto it = json.find(key);
    if (it == json.end() || it->is_null()) {
        return std::nullopt;
    }
    return it->get<std::vector<ButtonAction>>();
}

auto OptionalString(const nlohmann::json& json, std::string_view key) -> std::optional<std::string> {
    const auto it = json.find(key);
    if (it == json.end() || it->is_null()) {
        return std::nullopt;
    }
    return it->get<std::string>();
}

auto SubmitSystemToast(AppState& app, std::string title) -> void {
    Toast{.topic = ToastTopic::System, .title = std::move(title)}.Submit(app);
}

auto WatchSelector() -> OverlaySelector {
    return std::string(kWatchName);
}

} // namespace

auto from_json(const nlohmann::json& json, OverlaySelector& value) -> void {
    if (json.is_number()) {
        value = static_cast<OverlayId>(json.get<uint64_t>());
    } else {
        value = json.get<std::string>();
    }
}

auto from_json(const nlohmann::json& json, OverlayAction& value) -> void {
    const auto name = Json::VariantName(json);
    if (name == "ToggleVisible") {
        value = OverlayToggleVisible{};
    } else if (name == "ToggleImmovable") {
        value = OverlayToggleImmovable{};
    } else if (name == "ToggleInteraction") {
        value = OverlayToggleInteraction{};
    } else if (name == "Opacity") {
        value = OverlayOpacity{.delta = Json::VariantPayload(json).at("delta").get<float>()};
    } else {
        throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for OverlayAction", name), &json);
    }
}

auto from_json(const nlohmann::json& json, ButtonAction& value) -> void {
    const auto type = json.at("type").get<std::string>();
    if (type == "Exec") {
        value = ExecButtonAction{
            .command = json.at("command").get<std::vector<std::string>>(),
            .toast = OptionalString(json, "toast")
        };
    } else if (type == "VirtualKey") {
        const auto key_name = json.at("keycode").get<std::string>();
        const auto key = VirtualKeys::Parse(key_name);
        if (!key) {
            throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for VirtualKey", key_name), &json);
        }
        value = VirtualKeyButtonAction{.keycode = *key, .action = ParseEnum(json.at("action"), kPressRelease)};
    } else if (type == "Watch") {
        value = WatchButtonAction{.action = ParseWatchAction(json.at("action"))};
    } else if (type == "Overlay") {
        value = OverlayButtonAction{
            .target = json.at("target").get<OverlaySelector>(),
            .action = json.at("action").get<OverlayAction>()
        };
    } else if (type == "Window") {
        value = WindowButtonAction{
            .target = json.at("target").get<std::string>(),
            .action = ParseEnum(json.at("action"), kWindowActions)
        };
    } else if (type == "Toast") {
        ToastButtonAction toast{.message = json.at("message").get<std::string>(), .body = OptionalString(json, "body")};
        if (const auto it = json.find("seconds"); it != json.end() && !it->is_null()) {
            toast.seconds = it->get<float>();
        }
        value = std::move(toast);
    } else if (type == "ColorAdjust") {
        value = ColorAdjustButtonAction{
            .channel = ParseEnum(json.at("channel"), kColorChannels),
            .delta = json.at("delta").get<float>()
        };
    } else if (type == "DragMultiplier") {
        value = DragMultiplierButtonAction{.delta = json.at("delta").get<float>()};
    } else if (type == "System") {
        value = SystemButtonAction{.action = ParseEnum(json.at("action"), kSystemActions)};
    } else if (type == "SendOscValue") {
        SendOscButtonAction osc{.parameter = json.at("parameter").get<std::string>()};
        if (const auto it = json.find("values"); it != json.end() && !it->is_null()) {
            std::vector<OscValue> values;
            for (const auto& item : *it) {
                values.push_back(ParseOscValue(item));
            }
            osc.values = std::move(values);
        }
        value = std::move(osc);
    } else {
        throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for ButtonAction", type), &json);
    }
}

auto from_json(const nlohmann::json& json, ButtonData& value) -> void {
    value.click_down = OptionalActions(json, "click_down");
    value.click_up = OptionalActions(json, "click_up");
    value.long_click_up = OptionalActions(json, "long_click_up");
    value.right_down = OptionalActions(json, "right_down");
    value.right_up = OptionalActions(json, "right_up");
    value.long_right_up = OptionalActions(json, "long_right_up");
    value.middle_down = OptionalActions(json, "middle_down");
    value.middle_up = OptionalActions(json, "middle_up");
    value.long_middle_up = OptionalActions(json, "long_middle_up");
    value.scroll_down = OptionalActions(json, "scroll_down");
    value.scroll_up = OptionalActions(json, "scroll_up");
    if (const auto it = json.find("highlight"); it != json.end() && !it->is_null()) {
        value.highlight = ParseEnum(*it, kHighlightTests);
    }
}

ModularButton::ModularButton(ButtonData data) : data_(std::move(data)), last_down_(std::chrono::steady_clock::now()) {
}

auto ModularButton::PlayThump(AppState& app) -> void {
    app.audio.Play(std::as_bytes(std::span(kThumpSound)));
}

auto ModularButton::OnPress(AppState& app, PointerMode mode) -> void {
    last_down_ = std::chrono::steady_clock::now();
    last_mode_ = mode;
    switch (mode) {
    case PointerMode::Left: Run(app, data_.click_down); break;
    case PointerMode::Right: Run(app, data_.right_down); break;
    case PointerMode::Middle: Run(app, data_.middle_down); break;
    case PointerMode::Special: break;
    }
}

auto ModularButton::OnRelease(AppState& app) -> void {
    const auto held = std::chrono::duration<float>(std::chrono::steady_clock::now() - last_down_).count();
    const bool long_press = held > app.config.long_press_duration;
    switch (last_mode_) {
    case PointerMode::Left: Run(app, long_press ? data_.long_click_up : data_.click_up); break;
    case PointerMode::Right: Run(app, long_press ? data_.long_right_up : data_.right_up); break;
    case PointerMode::Middle: Run(app, long_press ? data_.long_middle_up : data_.middle_up); break;
    case PointerMode::Special: break;
    }
}

auto ModularButton::OnScroll(AppState& app, float delta_y, float) -> void {
    Run(app, delta_y < 0.0f ? data_.scroll_down : data_.scroll_up);
}

auto ModularButton::HighlightColor(AppState& app) -> std::optional<Eigen::Vector4f> {
    if (!data_.highlight) {
        return std::nullopt;
    }
    bool lit = false;
    switch (*data_.highlight) {
    case HighlightTest::AllowSliding: lit = app.config.allow_sliding; break;
    case HighlightTest::NotificationSounds: lit = app.config.notifications_sound_enabled; break;
    case HighlightTest::Notifications: lit = app.config.notifications_enabled; break;
    case HighlightTest::RotateLock: lit = !app.config.space_rotate_unlocked; break;
    }
    if (lit) {
        return Eigen::Vector4f(1.0f, 1.0f, 1.0f, 0.5f);
    }
    return std::nullopt;
}

auto ModularButton::Run(AppState& app, const std::optional<std::vector<ButtonAction>>& actions) -> void {
    if (!actions) {
        return;
    }
    for (const auto& action : *actions) {
        Run(app, action);
    }
}

auto ModularButton::Run(AppState& app, const ButtonAction& action) -> void {
    std::visit(
        [&](const auto& item) -> void {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, ExecButtonAction>) {
                RunExec(app, item);
            } else if constexpr (std::is_same_v<Item, WatchButtonAction>) {
                RunWatch(app, item.action);
            } else if constexpr (std::is_same_v<Item, OverlayButtonAction>) {
                RunOverlay(app, item.target, item.action);
            } else if constexpr (std::is_same_v<Item, WindowButtonAction>) {
                RunWindow(app, item.target, item.action);
            } else if constexpr (std::is_same_v<Item, VirtualKeyButtonAction>) {
                app.hid->SendKey(item.keycode, item.action == PressRelease::Press);
            } else if constexpr (std::is_same_v<Item, ToastButtonAction>) {
                Toast{
                    .topic = ToastTopic::System,
                    .title = item.message,
                    .body = item.body.value_or(""),
                    .timeout = item.seconds.value_or(5.0f),
                }
                    .Submit(app);
            } else if constexpr (std::is_same_v<Item, ColorAdjustButtonAction>) {
                app.tasks.Enqueue(SystemTask{ColorGainTask{.channel = item.channel, .delta = item.delta}});
            } else if constexpr (std::is_same_v<Item, SystemButtonAction>) {
                RunSystem(app, item.action);
            } else if constexpr (std::is_same_v<Item, DragMultiplierButtonAction>) {
                app.config.space_drag_multiplier += item.delta;
            } else if constexpr (std::is_same_v<Item, SendOscButtonAction>) {
#if FRAMEXR_FEATURE_OSC
                if (app.osc) {
                    if (!item.values) {
                        FatalError("SendOscValue requires values");
                    }
                    if (const auto sent = app.osc->SendMessage(item.parameter, *item.values); !sent) {
                        LOG_TRACE("Failed to send OSC value: {}", sent.error().Message());
                    }
                    PlayThump(app);
                }
#else
                Toast::ShowError(app, "OSC feature is not enabled");
#endif
            }
        },
        action
    );
}

auto ModularButton::RunExec(AppState& app, const ExecButtonAction& action) -> void {
    if (child_) {
        const auto status = child_->TryWait();
        if (!status) {
            child_.reset();
            Toast::ShowError(app, std::format("Error checking child process: {}", status.error().Message()));
        } else if (!status->has_value()) {
            LOG_WARN("Unable to launch child process: previous child not exited yet");
            return;
        } else {
            if (!(*status)->Success()) {
                Toast::ShowError(app, std::format("Child process exited with code: {}", (*status)->Describe()));
            }
            child_.reset();
        }
    }

    if (auto process = Process::Spawn(action.command)) {
        child_.emplace(std::move(*process));
        if (action.toast) {
            SubmitSystemToast(app, *action.toast);
        }
    } else {
        Toast::ShowError(app, std::format("Failed to spawn process {}: {}", action.command, process.error().Message()));
    }
}

auto ModularButton::RunSystem(AppState& app, SystemAction action) -> void {
    auto& config = app.config;
    const auto state_name = [](bool value) -> std::string_view { return kEnabledDisabled[value ? 1 : 0]; };
    switch (action) {
    case SystemAction::ToggleAllowSliding:
        config.allow_sliding = !config.allow_sliding;
        SubmitSystemToast(app, std::format("Sliding is {}.", state_name(config.allow_sliding)));
        break;
    case SystemAction::Recenter: app.tasks.Enqueue(SystemTask{RecenterTask{}}); break;
    case SystemAction::ToggleRotateLock:
        config.space_rotate_unlocked = !config.space_rotate_unlocked;
        SubmitSystemToast(app, std::format("Space rotate axis lock now {}.", state_name(!config.space_rotate_unlocked)));
        break;
    case SystemAction::PlayspaceResetOffset: app.tasks.Enqueue(SystemTask{ResetPlayspaceTask{}}); break;
    case SystemAction::PlayspaceFixFloor: {
        const auto now = std::chrono::steady_clock::now();
        for (int i = 0; i < 5; ++i) {
            Toast{
                .topic = ToastTopic::System,
                .title = std::format("Fixing floor in {}", 5 - i),
                .body = "Place either controller on the floor.",
                .timeout = 1.0f,
            }
                .SubmitAt(app, now + std::chrono::seconds(i));
        }
        app.tasks.EnqueueAt(SystemTask{FixFloorTask{}}, now + std::chrono::seconds(5));
        break;
    }
    case SystemAction::RecalculateExtent: FatalError("not yet implemented"); break;
    case SystemAction::ToggleNotifications:
        config.notifications_enabled = !config.notifications_enabled;
        SubmitSystemToast(app, std::format("Notifications are {}.", state_name(config.notifications_enabled)));
        break;
    case SystemAction::ToggleNotificationSounds:
        config.notifications_sound_enabled = !config.notifications_sound_enabled;
        SubmitSystemToast(app, std::format("Notification sounds are {}.", state_name(config.notifications_sound_enabled)));
        break;
    case SystemAction::PersistConfig:
        if (const auto result = config.SaveSettings(); !result) {
            Toast::ShowError(app, std::format("Failed to save config: {}", result.error().Message()));
        }
        break;
    case SystemAction::PersistLayout:
        if (const auto result = config.SaveLayout(); !result) {
            Toast::ShowError(app, std::format("Failed to save layout: {}", result.error().Message()));
        }
        break;
    }
}

auto ModularButton::RunWatch(AppState& app, const WatchAction& action) -> void {
    std::visit(
        [&](const auto& item) -> void {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, WatchHide>) {
                app.tasks.Enqueue(
                    ModifyOverlayTask{
                        .selector = WatchSelector(),
                        .modify = [](AppState& state, OverlayState& watch) -> void {
                            if (!watch.saved_transform) {
                                watch.want_visible = false;
                                watch.saved_transform = watch.transform;
                                Toast{
                                    .topic = ToastTopic::System,
                                    .title = "Watch hidden",
                                    .body = "Use show/hide binding to restore.",
                                    .timeout = 3.0f,
                                }
                                    .Submit(state);
                            } else {
                                watch.want_visible = true;
                                watch.saved_transform.reset();
                                SubmitSystemToast(state, "Watch restored");
                            }
                        },
                    }
                );
                PlayThump(app);
            } else if constexpr (std::is_same_v<Item, WatchSwitchHands>) {
                app.tasks.Enqueue(
                    ModifyOverlayTask{
                        .selector = WatchSelector(),
                        .modify = [](AppState& state, OverlayState& watch) -> void {
                            if (watch.relative_to == RelativeTo::LeftHand) {
                                watch.relative_to = RelativeTo::RightHand;
                                watch.spawn_rotation = state.config.watch_rot
                                                     * Eigen::AngleAxisf(std::numbers::pi_v<float>, Eigen::Vector3f::UnitX())
                                                     * Eigen::AngleAxisf(std::numbers::pi_v<float>, Eigen::Vector3f::UnitZ());
                                watch.spawn_point = state.config.watch_pos;
                                watch.spawn_point.x() *= -1.0f;
                            } else {
                                watch.relative_to = RelativeTo::LeftHand;
                                watch.spawn_rotation = state.config.watch_rot;
                                watch.spawn_point = state.config.watch_pos;
                            }
                            watch.dirty = true;
                            Toast{
                                .topic = ToastTopic::System,
                                .title = "Watch switched",
                                .body = "Check your other hand",
                                .timeout = 3.0f,
                            }
                                .Submit(state);
                        },
                    }
                );
                PlayThump(app);
            } else if constexpr (std::is_same_v<Item, WatchViewAngle>) {
                auto& config = app.config;
                if (item.kind == ViewAngleKind::MinOpacity) {
                    const float diff = (config.watch_view_angle_max - config.watch_view_angle_min) + item.delta;
                    config.watch_view_angle_min = std::clamp(
                        config.watch_view_angle_max - diff,
                        0.0f,
                        config.watch_view_angle_max - 0.05f
                    );
                } else {
                    const float diff = config.watch_view_angle_max - config.watch_view_angle_min;
                    config.watch_view_angle_max = std::clamp(config.watch_view_angle_max + item.delta, 0.05f, 1.0f);
                    config.watch_view_angle_min = std::clamp(
                        config.watch_view_angle_max - diff,
                        0.0f,
                        config.watch_view_angle_max - 0.05f
                    );
                }
            } else if constexpr (std::is_same_v<Item, WatchRotation>) {
                const auto axis = item.axis == Axis::X ? Eigen::Vector3f::UnitX()
                                : item.axis == Axis::Y ? Eigen::Vector3f::UnitY()
                                                       : Eigen::Vector3f::UnitZ();
                const Eigen::Quaternionf rotation(Eigen::AngleAxisf(item.delta * std::numbers::pi_v<float> / 180.0f, axis));
                app.tasks.Enqueue(
                    ModifyOverlayTask{
                        .selector = WatchSelector(),
                        .modify = [rotation](AppState& state, OverlayState& watch) -> void {
                            watch.spawn_rotation *= rotation;
                            state.config.watch_rot = watch.spawn_rotation;
                            watch.dirty = true;
                        },
                    }
                );
            } else {
                const auto axis = static_cast<Eigen::Index>(item.axis);
                const float delta = item.delta;
                app.tasks.Enqueue(
                    ModifyOverlayTask{
                        .selector = WatchSelector(),
                        .modify = [axis, delta](AppState& state, OverlayState& watch) -> void {
                            watch.spawn_point[axis] += delta;
                            state.config.watch_pos = watch.spawn_point;
                            watch.dirty = true;
                        },
                    }
                );
            }
        },
        action
    );
}

auto ModularButton::RunOverlay(AppState& app, const OverlaySelector& target, const OverlayAction& action) -> void {
    std::visit(
        [&](const auto& item) -> void {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, OverlayToggleVisible>) {
                app.tasks.Enqueue(
                    ModifyOverlayTask{
                        .selector = target,
                        .modify = [](AppState& state, OverlayState& overlay) -> void {
                            overlay.want_visible = !overlay.want_visible;
                            if (overlay.recenter) {
                                overlay.show_hide = overlay.want_visible;
                                overlay.Reset(state, false);
                            }
                            auto& screens = state.config.show_screens;
                            bool changed = false;
                            if (!overlay.want_visible) {
                                changed = std::erase(screens, overlay.name) > 0;
                            } else if (std::ranges::find(screens, overlay.name) == screens.end()) {
                                screens.push_back(overlay.name);
                                changed = true;
                            }
                            if (changed) {
                                if (const auto result = state.config.SaveLayout()) {
                                    LOG_DEBUG("Saved state");
                                } else {
                                    Toast::ShowError(state, std::format("Failed to save state: {}", result.error().Message()));
                                }
                            }
                        },
                    }
                );
            } else if constexpr (std::is_same_v<Item, OverlayToggleImmovable>) {
                app.tasks.Enqueue(
                    ModifyOverlayTask{
                        .selector = target,
                        .modify = [](AppState& state, OverlayState& overlay) -> void {
                            overlay.recenter = !overlay.recenter;
                            overlay.grabbable = overlay.recenter;
                            overlay.show_hide = overlay.recenter;
                            SubmitSystemToast(
                                state,
                                overlay.recenter ? std::format("{} is now unlocked!", overlay.name)
                                                 : std::format("{} is now locked in place!", overlay.name)
                            );
                        },
                    }
                );
                PlayThump(app);
            } else if constexpr (std::is_same_v<Item, OverlayToggleInteraction>) {
                app.tasks.Enqueue(
                    ModifyOverlayTask{
                        .selector = target,
                        .modify = [](AppState& state, OverlayState& overlay) -> void {
                            overlay.interactable = !overlay.interactable;
                            SubmitSystemToast(
                                state,
                                overlay.interactable ? std::format("{} is now interactable!", overlay.name)
                                                     : std::format("{} is now non-interactable!", overlay.name)
                            );
                        },
                    }
                );
                PlayThump(app);
            } else {
                const float delta = item.delta;
                app.tasks.Enqueue(
                    ModifyOverlayTask{
                        .selector = target,
                        .modify = [delta](AppState&, OverlayState& overlay) -> void {
                            overlay.alpha = std::clamp(overlay.alpha + delta, 0.1f, 1.0f);
                            overlay.dirty = true;
                            LOG_DEBUG("{}: alpha {}", overlay.name, overlay.alpha);
                        },
                    }
                );
            }
        },
        action
    );
}

auto ModularButton::RunWindow(AppState& app, const std::string& target, WindowAction action) -> void {
    switch (action) {
    case WindowAction::ShowMirror:
#if FRAMEXR_FEATURE_WAYLAND
        app.tasks.Enqueue(
            CreateOverlayTask{
                .selector = target,
                .create = [target](AppState& state) -> std::optional<std::pair<OverlayState, std::unique_ptr<OverlayBackend>>> {
                    Toast{.topic = ToastTopic::System, .title = "Check your desktop for popup.", .sound = true}.Submit(state);
                    return MirrorRenderer::Create(target, false, state.config);
                },
            }
        );
#else
        LOG_WARN("Mirror not available without Wayland feature.");
#endif
        break;
    case WindowAction::ShowUi:
        app.tasks.Enqueue(
            CreateOverlayTask{
                .selector = target,
                .create = [target](AppState& state) -> std::optional<std::pair<OverlayState, std::unique_ptr<OverlayBackend>>> {
                    return CustomUi::Create(state, target);
                },
            }
        );
        break;
    case WindowAction::Destroy: app.tasks.Enqueue(DropOverlayTask{.selector = target}); break;
    }
}
