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

#include "Toast.hpp"

#include <array>
#include <format>
#include <numbers>
#include <optional>
#include <utility>

#include "AudioOutput.hpp"
#include "Canvas.hpp"
#include "Config.hpp"
#include "Device.hpp"
#include "Font.hpp"
#include "Json.hpp"
#include "Logger.hpp"
#include "Task.hpp"

namespace {

constexpr auto kTopicNames = std::to_array<std::pair<ToastTopic, std::string_view>>({
    {ToastTopic::System, "System"},
    {ToastTopic::DesktopNotification, "DesktopNotification"},
    {ToastTopic::XsNotification, "XSNotification"},
    {ToastTopic::IpdChange, "IpdChange"},
});

constexpr auto kDisplayNames = std::to_array<std::pair<ToastDisplay, std::string_view>>({
    {ToastDisplay::Hide, "Hide"},
    {ToastDisplay::Center, "Center"},
    {ToastDisplay::Watch, "Watch"},
});

constexpr int kFontSize = 16;
constexpr float kPaddingX = 25.0f;
constexpr float kPixelsToMeters = 1.0f / 2000.0f;
constexpr std::string_view kToastName = "toast";

auto Color(std::string_view hex) -> Eigen::Vector4f {
    return Canvas::ParseColor(hex).value_or(Canvas::FallbackColor());
}

} // namespace

auto from_json(const nlohmann::json& json, ToastTopic& value) -> void {
    const auto name = Json::VariantName(json);
    for (const auto& [topic, topic_name] : kTopicNames) {
        if (topic_name == name) {
            value = topic;
            return;
        }
    }
    throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for ToastTopic", name), &json);
}

auto to_json(nlohmann::json& json, const ToastTopic& value) -> void {
    for (const auto& [topic, topic_name] : kTopicNames) {
        if (topic == value) {
            json = topic_name;
        }
    }
}

auto from_json(const nlohmann::json& json, ToastDisplay& value) -> void {
    const auto name = Json::VariantName(json);
    for (const auto& [display, display_name] : kDisplayNames) {
        if (display_name == name) {
            value = display;
            return;
        }
    }
    throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for DisplayMethod", name), &json);
}

auto to_json(nlohmann::json& json, const ToastDisplay& value) -> void {
    for (const auto& [display, display_name] : kDisplayNames) {
        if (display == value) {
            json = display_name;
        }
    }
}

auto ToastOverlay::Create(const Toast& toast) -> std::optional<std::pair<OverlayState, std::unique_ptr<OverlayBackend>>> {
    const auto& topics = g_config->notification_topics;
    const auto configured = topics.find(toast.topic);
    const auto method = configured != topics.end()           ? configured->second
                      : toast.topic == ToastTopic::IpdChange ? ToastDisplay::Hide
                                                             : ToastDisplay::Center;

    Eigen::Vector3f spawn_point = Eigen::Vector3f::Zero();
    Eigen::Quaternionf spawn_rotation = Eigen::Quaternionf::Identity();
    RelativeTo relative_to = RelativeTo::Head;
    switch (method) {
    case ToastDisplay::Hide: return std::nullopt;
    case ToastDisplay::Center: spawn_point = Eigen::Vector3f(0.0f, -0.2f, -0.5f); break;
    case ToastDisplay::Watch:
        spawn_point = g_config->watch_pos + Eigen::Vector3f(-0.005f, -0.05f, 0.02f);
        spawn_rotation = g_config->watch_rot;
        if (g_config->watch_hand == Handedness::Left) {
            relative_to = RelativeTo::LeftHand;
        } else {
            spawn_point.x() = -spawn_point.x();
            spawn_rotation = spawn_rotation * Eigen::AngleAxisf(std::numbers::pi_v<float>, Eigen::Vector3f::UnitX())
                           * Eigen::AngleAxisf(std::numbers::pi_v<float>, Eigen::Vector3f::UnitZ());
            relative_to = RelativeTo::RightHand;
        }
        break;
    }

    const std::string title = toast.title.empty() ? "Notification" : toast.title;

    TextSize size;
    if (toast.body.empty()) {
        const auto title_size = g_fonts->MeasureText(title, kFontSize);
        if (!title_size) {
            return std::nullopt;
        }
        size = {.width = title_size->width, .height = title_size->height + 20.0f};
    } else {
        const auto title_size = g_fonts->MeasureText(title, kFontSize);
        const auto body_size = g_fonts->MeasureText(toast.body, kFontSize);
        if (!title_size || !body_size) {
            return std::nullopt;
        }
        size = {.width = std::max(title_size->width, body_size->width), .height = body_size->height + 50.0f};
    }

    const float text_width = size.width;
    size.width += kPaddingX * 2.0f;

    auto created = Canvas::Create(static_cast<uint32_t>(size.width), static_cast<uint32_t>(size.height), g_device->NativeFormat());
    if (!created) {
        return std::nullopt;
    }
    auto& canvas = **created;

    canvas.SetFontSize(kFontSize);
    canvas.SetFgColor(Color("@text"));
    canvas.SetBgColor(Color("@panel"));
    canvas.Panel(0.0f, 0.0f, size.width, size.height, 16.0f);

    if (toast.body.empty()) {
        canvas.LabelCentered(kPaddingX, 0.0f, text_width, size.height, 16.0f, title);
    } else {
        canvas.Label(kPaddingX, 54.0f, text_width, size.height - 54.0f, 3.0f, toast.body);
        canvas.SetFgColor(Color("@soft-text"));
        canvas.SetBgColor(Color("@background"));
        canvas.Panel(0.0f, 0.0f, size.width, 30.0f, 16.0f);
        canvas.LabelCentered(kPaddingX, 16.0f, text_width, static_cast<float>(kFontSize) + 2.0f, 16.0f, title);
    }

    OverlayState state;
    state.SetName(std::string(kToastName));
    state.SetWantVisible(true);
    state.SetSpawnScale(size.width * kPixelsToMeters);
    state.SetSpawnRotation(spawn_rotation);
    state.SetSpawnPoint(spawn_point);
    state.SetZOrder(kZOrderToast);
    state.SetAttachedTo(relative_to);
    return std::pair<OverlayState, std::unique_ptr<OverlayBackend>>{std::move(state), std::move(*created)};
}

auto ToastOverlay::Show(const Toast& toast) -> void {
    ShowAt(toast, std::chrono::steady_clock::now());
}

auto ToastOverlay::ShowAt(const Toast& toast, std::chrono::steady_clock::time_point instant) -> void {
    const auto destroy_at = instant
                          + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                std::chrono::duration<float>(toast.timeout)
                          );

    if (toast.sound && g_config->notifications_sound_enabled) {
        g_audio->PlayNotification();
    }

    g_tasks->EnqueueAt(DropOverlayTask{.selector = std::string(kToastName)}, instant);
    g_tasks->EnqueueAt(
        CreateOverlayTask{
            .selector = std::string(kToastName),
            .create = [toast, destroy_at]() -> std::optional<std::pair<OverlayState, std::unique_ptr<OverlayBackend>>> {
                auto created = Create(toast);
                if (created) {
                    created->first.AutoMovement();
                    g_tasks->EnqueueAt(DropOverlayTask{.selector = created->first.Id()}, destroy_at);
                }
                return created;
            },
        },
        instant
    );
}

auto ToastOverlay::ShowError(std::string_view message) -> void {
    LOG_ERROR("{}", message);
    Show(Toast{.topic = ToastTopic::System, .title = "Error", .body = std::string(message), .timeout = 3.0f});
}
