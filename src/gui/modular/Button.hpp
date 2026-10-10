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

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <Eigen/Core>
#include <nlohmann/json.hpp>

#include "Control.hpp"
#include "Osc.hpp"
#include "Process.hpp"
#include "Task.hpp"
#include "VirtualKey.hpp"

class AppState;

enum class PressRelease : uint8_t {
    Release,
    Press,
};

enum class ViewAngleKind : uint8_t {
    MinOpacity,
    MaxOpacity,
};

enum class Axis : uint8_t {
    X,
    Y,
    Z,
};

enum class HighlightTest : uint8_t {
    AllowSliding,
    NotificationSounds,
    Notifications,
    RotateLock,
};

enum class SystemAction : uint8_t {
    ToggleAllowSliding,
    Recenter,
    ShowHide,
    ToggleNotificationSounds,
    ToggleNotifications,
    ToggleRotateLock,
    PlayspaceResetOffset,
    PlayspaceFixFloor,
    RecalculateExtent,
    PersistConfig,
    PersistLayout,
};

enum class WindowAction : uint8_t {
    ShowMirror,
    ShowUi,
    Destroy,
};

struct WatchHide {};
struct WatchSwitchHands {};

struct WatchViewAngle {
    ViewAngleKind kind = ViewAngleKind::MinOpacity;
    float delta = 0.0f;
};

struct WatchRotation {
    Axis axis = Axis::X;
    float delta = 0.0f;
};

struct WatchPosition {
    Axis axis = Axis::X;
    float delta = 0.0f;
};

using WatchAction = std::variant<WatchHide, WatchSwitchHands, WatchViewAngle, WatchRotation, WatchPosition>;

struct OverlayToggleVisible {};
struct OverlayToggleImmovable {};
struct OverlayToggleInteraction {};

struct OverlayOpacity {
    float delta = 0.0f;
};

using OverlayAction = std::variant<OverlayToggleVisible, OverlayToggleImmovable, OverlayToggleInteraction, OverlayOpacity>;

struct ExecButtonAction {
    std::vector<std::string> command;
    std::optional<std::string> toast;
};

struct VirtualKeyButtonAction {
    VirtualKey keycode = VirtualKey::Escape;
    PressRelease action = PressRelease::Press;
};

struct WatchButtonAction {
    WatchAction action;
};

struct OverlayButtonAction {
    OverlaySelector target;
    OverlayAction action;
};

struct WindowButtonAction {
    std::string target;
    WindowAction action = WindowAction::ShowUi;
};

struct ToastButtonAction {
    std::string message;
    std::optional<std::string> body;
    std::optional<float> seconds;
};

struct ColorAdjustButtonAction {
    ColorChannel channel = ColorChannel::All;
    float delta = 0.0f;
};

struct DragMultiplierButtonAction {
    float delta = 0.0f;
};

struct SystemButtonAction {
    SystemAction action = SystemAction::PersistConfig;
};

struct SendOscButtonAction {
    std::string parameter;
    std::optional<std::vector<OscValue>> values;
};

using ButtonAction = std::variant<
    ExecButtonAction,
    VirtualKeyButtonAction,
    WatchButtonAction,
    OverlayButtonAction,
    WindowButtonAction,
    ToastButtonAction,
    ColorAdjustButtonAction,
    DragMultiplierButtonAction,
    SystemButtonAction,
    SendOscButtonAction>;

struct ButtonData {
    std::optional<std::vector<ButtonAction>> click_down;
    std::optional<std::vector<ButtonAction>> click_up;
    std::optional<std::vector<ButtonAction>> long_click_up;
    std::optional<std::vector<ButtonAction>> right_down;
    std::optional<std::vector<ButtonAction>> right_up;
    std::optional<std::vector<ButtonAction>> long_right_up;
    std::optional<std::vector<ButtonAction>> middle_down;
    std::optional<std::vector<ButtonAction>> middle_up;
    std::optional<std::vector<ButtonAction>> long_middle_up;
    std::optional<std::vector<ButtonAction>> scroll_down;
    std::optional<std::vector<ButtonAction>> scroll_up;
    std::optional<HighlightTest> highlight;
};

auto from_json(const nlohmann::json& json, OverlaySelector& value) -> void;
auto from_json(const nlohmann::json& json, OverlayAction& value) -> void;
auto from_json(const nlohmann::json& json, ButtonAction& value) -> void;
auto from_json(const nlohmann::json& json, ButtonData& value) -> void;

class ModularButton final : public Control {
  public:
    explicit ModularButton(ButtonData data);

    auto OnPress(AppState& app, PointerMode mode) -> void override;
    auto OnRelease(AppState& app) -> void override;
    auto OnScroll(AppState& app, float delta_y, float delta_x) -> void override;
    auto HighlightColor(AppState& app) -> std::optional<Eigen::Vector4f> override;

    static auto PlayThump(AppState& app) -> void;

  private:
    auto Run(AppState& app, const std::optional<std::vector<ButtonAction>>& actions) -> void;
    auto Run(AppState& app, const ButtonAction& action) -> void;
    auto RunExec(AppState& app, const ExecButtonAction& action) -> void;

    static auto RunSystem(AppState& app, SystemAction action) -> void;
    static auto RunWatch(AppState& app, const WatchAction& action) -> void;
    static auto RunOverlay(AppState& app, const OverlaySelector& target, const OverlayAction& action) -> void;
    static auto RunWindow(AppState& app, const std::string& target, WindowAction action) -> void;

    ButtonData data_;
    std::chrono::steady_clock::time_point last_down_;
    PointerMode last_mode_ = PointerMode::Left;
    std::optional<Process> child_;
};
