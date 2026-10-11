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

#include "Canvas.hpp"
#include "Font.hpp"
#include "Json.hpp"
#include "Logger.hpp"
#include "Overlay.hpp"
#include "State.hpp"

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

auto CreateToast(const Toast& toast, AppState& app) -> std::optional<std::pair<OverlayState, std::unique_ptr<OverlayBackend>>> {
    const auto method = app.toast_topics.contains(toast.topic) ? app.toast_topics.at(toast.topic) : ToastDisplay::Hide;

    Eigen::Vector3f spawn_point = Eigen::Vector3f::Zero();
    Eigen::Quaternionf spawn_rotation = Eigen::Quaternionf::Identity();
    RelativeTo relative_to = RelativeTo::Head;
    switch (method) {
    case ToastDisplay::Hide: return std::nullopt;
    case ToastDisplay::Center: spawn_point = Eigen::Vector3f(0.0f, -0.2f, -0.5f); break;
    case ToastDisplay::Watch:
        spawn_point = app.config.watch_pos + Eigen::Vector3f(-0.005f, -0.05f, 0.02f);
        spawn_rotation = app.config.watch_rot;
        if (app.config.watch_hand == Handedness::Left) {
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
    auto& device = *app.device;

    TextSize size;
    if (toast.body.empty()) {
        const auto title_size = app.fonts->MeasureText(title, kFontSize, device);
        if (!title_size) {
            return std::nullopt;
        }
        size = {.width = title_size->width, .height = title_size->height + 20.0f};
    } else {
        const auto title_size = app.fonts->MeasureText(title, kFontSize, device);
        const auto body_size = app.fonts->MeasureText(toast.body, kFontSize, device);
        if (!title_size || !body_size) {
            return std::nullopt;
        }
        size = {.width = std::max(title_size->width, body_size->width), .height = body_size->height + 50.0f};
    }

    const float text_width = size.width;
    size.width += kPaddingX * 2.0f;

    auto created = Canvas::Create(
        app.device,
        static_cast<uint32_t>(size.width),
        static_cast<uint32_t>(size.height),
        device.NativeFormat()
    );
    if (!created) {
        return std::nullopt;
    }
    auto& canvas = **created;

    canvas.font_size = kFontSize;
    canvas.fg_color = Color("@text");
    canvas.bg_color = Color("@panel");
    canvas.Panel(0.0f, 0.0f, size.width, size.height, 16.0f);

    if (toast.body.empty()) {
        canvas.LabelCentered(kPaddingX, 0.0f, text_width, size.height, 16.0f, title);
    } else {
        canvas.Label(kPaddingX, 54.0f, text_width, size.height - 54.0f, 3.0f, toast.body);
        canvas.fg_color = Color("@soft-text");
        canvas.bg_color = Color("@background");
        canvas.Panel(0.0f, 0.0f, size.width, 30.0f, 16.0f);
        canvas.LabelCentered(kPaddingX, 16.0f, text_width, static_cast<float>(kFontSize) + 2.0f, 16.0f, title);
    }

    OverlayState state;
    state.name = kToastName;
    state.want_visible = true;
    state.spawn_scale = size.width * kPixelsToMeters;
    state.spawn_rotation = spawn_rotation;
    state.spawn_point = spawn_point;
    state.z_order = kZOrderToast;
    state.relative_to = relative_to;
    return std::pair<OverlayState, std::unique_ptr<OverlayBackend>>{std::move(state), std::move(*created)};
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

auto Toast::Submit(AppState& app) const -> void {
    SubmitAt(app, std::chrono::steady_clock::now());
}

auto Toast::SubmitAt(AppState& app, std::chrono::steady_clock::time_point instant) const -> void {
    const auto destroy_at = instant
                          + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(timeout));

    if (sound && app.config.notifications_sound_enabled) {
        app.audio.Play(app.toast_sound);
    }

    app.tasks.EnqueueAt(DropOverlayTask{.selector = std::string(kToastName)}, instant);
    app.tasks.EnqueueAt(
        CreateOverlayTask{
            .selector = std::string(kToastName),
            .create = [toast = *this,
                       destroy_at](AppState& state) -> std::optional<std::pair<OverlayState, std::unique_ptr<OverlayBackend>>> {
                auto created = CreateToast(toast, state);
                if (created) {
                    created->first.AutoMovement(state);
                    state.tasks.EnqueueAt(DropOverlayTask{.selector = created->first.id}, destroy_at);
                }
                return created;
            },
        },
        instant
    );
}

auto Toast::ShowError(AppState& app, std::string_view message) -> void {
    LOG_ERROR("{}", message);
    Toast{.topic = ToastTopic::System, .title = "Error", .body = std::string(message), .timeout = 3.0f}.Submit(app);
}
