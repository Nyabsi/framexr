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
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <openxr/openxr.h>

#include "Error.hpp"
#include "Input.hpp"
#include "OpenXrState.hpp"

class AppState;
class Monado;
class OpenXrInstance;
struct Config;

struct OpenXrActionBinding {
    std::optional<std::string> left;
    std::optional<std::string> right;
    std::optional<std::array<float, 2>> threshold;
    std::optional<bool> double_click;
    std::optional<bool> triple_click;
};

struct OpenXrActionProfile {
    std::string profile;
    std::optional<OpenXrActionBinding> pose;
    std::optional<OpenXrActionBinding> click;
    std::optional<OpenXrActionBinding> grab;
    std::optional<OpenXrActionBinding> alt_click;
    std::optional<OpenXrActionBinding> space_drag;
    std::optional<OpenXrActionBinding> space_rotate;
    std::optional<OpenXrActionBinding> space_reset;
    std::optional<OpenXrActionBinding> click_modifier_right;
    std::optional<OpenXrActionBinding> click_modifier_middle;
    std::optional<OpenXrActionBinding> move_mouse;
    std::optional<OpenXrActionBinding> scroll;
    std::optional<OpenXrActionBinding> haptic;
};

class OpenXrMultiClick final {
  public:
    OpenXrMultiClick() = default;

    static auto Create(
        const OpenXrInstance& instance,
        XrActionSet action_set,
        std::string_view action_name,
        std::string_view side,
        size_t count
    ) -> Result<OpenXrMultiClick>;

    [[nodiscard]] auto BoolAction() const -> XrAction { return bool_action_; }
    [[nodiscard]] auto FloatAction() const -> XrAction { return float_action_; }

    auto Check(const OpenXrState& xr, float threshold) -> Result<bool>;

  private:
    OpenXrMultiClick(std::string name, XrAction bool_action, XrAction float_action, size_t count);

    std::string name_;
    XrAction bool_action_ = XR_NULL_HANDLE;
    XrAction float_action_ = XR_NULL_HANDLE;
    size_t count_ = 0;
    std::vector<std::chrono::steady_clock::time_point> previous_;
    bool held_active_ = false;
    bool held_inactive_ = false;
};

class OpenXrClickAction final {
  public:
    OpenXrClickAction() = default;

    static auto Create(const OpenXrInstance& instance, XrActionSet action_set, std::string_view name, std::string_view side)
        -> Result<OpenXrClickAction>;

    [[nodiscard]] auto Handler(size_t count) const -> const OpenXrMultiClick& { return handlers_.at(count); }

    auto State(bool before, const OpenXrState& xr, const Config& config) -> Result<bool>;

  private:
    explicit OpenXrClickAction(std::array<OpenXrMultiClick, 3> handlers);

    std::array<OpenXrMultiClick, 3> handlers_;
};

struct OpenXrHandActions {
    XrAction pose = XR_NULL_HANDLE;
    XrAction scroll = XR_NULL_HANDLE;
    XrAction haptics = XR_NULL_HANDLE;
    OpenXrClickAction click;
    OpenXrClickAction grab;
    OpenXrClickAction alt_click;
    OpenXrClickAction space_drag;
    OpenXrClickAction space_rotate;
    OpenXrClickAction space_reset;
    OpenXrClickAction modifier_right;
    OpenXrClickAction modifier_middle;
    OpenXrClickAction move_mouse;

    static auto Create(const OpenXrInstance& instance, XrActionSet action_set, std::string_view side) -> Result<OpenXrHandActions>;
};

struct OpenXrHand {
    OpenXrHandActions actions;
    XrSpace space = XR_NULL_HANDLE;

    auto Update(Pointer& pointer, const OpenXrState& xr, const Config& config) -> Result<>;
};

class OpenXrInput final {
  public:
    static auto Create(const OpenXrState& xr) -> Result<std::unique_ptr<OpenXrInput>>;
    static auto UpdateDevices(AppState& app, Monado& monado) -> void;

    ~OpenXrInput();
    OpenXrInput(const OpenXrInput&) = delete;
    auto operator=(const OpenXrInput&) -> OpenXrInput& = delete;

    auto Update(const OpenXrState& xr, AppState& app) -> Result<>;
    auto ApplyHaptics(const OpenXrState& xr, size_t hand, const Haptics& haptics) -> void;

  private:
    OpenXrInput(XrActionSet action_set, std::array<OpenXrHand, 2> hands);

    static auto LoadProfiles() -> std::vector<OpenXrActionProfile>;
    static auto ParseProfiles(const nlohmann::json& json) -> Result<std::vector<OpenXrActionProfile>>;
    static auto SuggestBindings(const OpenXrInstance& instance, const std::array<OpenXrHandActions, 2>& hands) -> void;

    XrActionSet action_set_;
    std::array<OpenXrHand, 2> hands_;
};
