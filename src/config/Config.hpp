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

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Eigen/Geometry>
#include <nlohmann/json.hpp>

#include "Error.hpp"
#include "Json.hpp"
#include "Logger.hpp"
#include "Yaml.hpp"

enum class Handedness : uint8_t {
    Left,
    Right,
};

auto from_json(const nlohmann::json& json, Handedness& value) -> void;
auto to_json(nlohmann::json& json, const Handedness& value) -> void;

enum class ToastTopic : uint8_t {
    System,
    DesktopNotification,
    XsNotification,
    IpdChange,
};

enum class ToastDisplay : uint8_t {
    Hide,
    Center,
    Watch,
};

auto from_json(const nlohmann::json& json, ToastTopic& value) -> void;
auto to_json(nlohmann::json& json, const ToastTopic& value) -> void;
auto from_json(const nlohmann::json& json, ToastDisplay& value) -> void;
auto to_json(nlohmann::json& json, const ToastDisplay& value) -> void;

template <>
class nlohmann::adl_serializer<std::map<ToastTopic, ToastDisplay>> {
  public:
    static auto from_json(const nlohmann::json& json, std::map<ToastTopic, ToastDisplay>& value) -> void;
    static auto to_json(nlohmann::json& json, const std::map<ToastTopic, ToastDisplay>& value) -> void;
};

struct Config {
    Eigen::Vector3f watch_pos{-0.03f, -0.01f, 0.125f};
    Eigen::Quaternionf watch_rot{0.0f, -0.707'106'6f, 0.000'796'361'8f, 0.707'106'6f};
    Handedness watch_hand = Handedness::Left;
    uint32_t click_freeze_time_ms = 300;
    uint32_t mouse_move_interval_ms = 10;
    bool notifications_enabled = true;
    bool notifications_sound_enabled = true;
    std::map<ToastTopic, ToastDisplay> notification_topics;
    std::string notification_sound;
    bool keyboard_sound_enabled = true;
    float keyboard_scale = 1.0f;
    std::string keyboard_profile;
    std::string keyboard_region;
    std::string theme = "macchiato";
    std::string watch_profile = "default";
    float desktop_view_scale = 1.0f;
    float watch_view_angle_min = 0.5f;
    float watch_view_angle_max = 0.7f;
    float long_press_duration = 1.0f;
    uint16_t osc_out_port = 9000;
    bool upright_screen_fix = false;
    bool double_cursor_fix = false;
    std::vector<std::string> show_screens;
    std::vector<std::pair<std::string, float>> curve_values;
    std::vector<std::pair<std::string, Eigen::Affine3f>> transform_values;
    std::string capture_method = "auto";
    float xr_grab_sensitivity = 0.7f;
    float xr_click_sensitivity = 0.7f;
    float xr_alt_click_sensitivity = 0.7f;
    float xr_grab_sensitivity_release = 0.5f;
    float xr_click_sensitivity_release = 0.5f;
    float xr_alt_click_sensitivity_release = 0.5f;
    bool allow_sliding = true;
    bool grab_smoothing = true;
    bool pointer_smoothing = true;
    bool screen_lowpass_filter = true;
    bool focus_follows_mouse_mode = false;
    bool block_game_input = false;
    bool block_game_input_ignore_watch = true;
    std::string primary_font = "LiberationSans:style=Bold";
    float space_drag_multiplier = 1.0f;
    std::string skybox_texture;
    bool use_skybox = true;
    bool use_passthrough = true;
    uint16_t screen_max_height = 1440;
    bool screen_render_down = false;
    float pointer_lerp_factor = 0.3f;
    bool space_rotate_unlocked = false;
    std::vector<std::string> alt_click_down;
    std::vector<std::string> alt_click_up;
    std::vector<std::string> timezones{"America/New_York", "Asia/Tokyo"};
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    Config,
    watch_pos,
    watch_rot,
    watch_hand,
    click_freeze_time_ms,
    mouse_move_interval_ms,
    notifications_enabled,
    notifications_sound_enabled,
    notification_topics,
    notification_sound,
    keyboard_sound_enabled,
    keyboard_scale,
    keyboard_profile,
    keyboard_region,
    theme,
    watch_profile,
    desktop_view_scale,
    watch_view_angle_min,
    watch_view_angle_max,
    long_press_duration,
    osc_out_port,
    upright_screen_fix,
    double_cursor_fix,
    show_screens,
    curve_values,
    transform_values,
    capture_method,
    xr_grab_sensitivity,
    xr_click_sensitivity,
    xr_alt_click_sensitivity,
    xr_grab_sensitivity_release,
    xr_click_sensitivity_release,
    xr_alt_click_sensitivity_release,
    allow_sliding,
    grab_smoothing,
    pointer_smoothing,
    screen_lowpass_filter,
    focus_follows_mouse_mode,
    block_game_input,
    block_game_input_ignore_watch,
    primary_font,
    space_drag_multiplier,
    skybox_texture,
    use_skybox,
    use_passthrough,
    screen_max_height,
    screen_render_down,
    pointer_lerp_factor,
    space_rotate_unlocked,
    alt_click_down,
    alt_click_up,
    timezones
)

extern Config* g_config;
