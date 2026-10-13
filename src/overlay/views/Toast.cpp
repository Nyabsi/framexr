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
#include <cmath>
#include <format>
#include <numbers>
#include <optional>
#include <utility>

#include "AudioPlayback.hpp"
#include "Compose.hpp"
#include "Config.hpp"
#include "FontCache.hpp"
#include "Form.hpp"
#include "FormTheme.hpp"
#include "LabelComponent.hpp"
#include "Logger.hpp"
#include "PanelComponent.hpp"
#include "TaskQueue.hpp"

namespace {

constexpr int kFontSize = 16;
constexpr float kPaddingX = 25.0f;
constexpr float kPixelsToMeters = 1.0f / 2000.0f;
constexpr std::string_view kToastName = "toast";

auto Color(std::string_view hex) -> Eigen::Vector4f {
    return FormTheme::ParseColor(hex).value_or(FormTheme::FallbackColor());
}

} // namespace

auto Toast::Create(const ToastMessage& toast) -> std::unique_ptr<Toast> {
    const auto& topics = g_config->notification_topics;
    const auto configured = topics.find(toast.topic);
    const auto method = configured != topics.end()           ? configured->second
                      : toast.topic == ToastTopic::IpdChange ? ToastDisplay::Hide
                                                             : ToastDisplay::Center;

    Eigen::Vector3f spawn_point = Eigen::Vector3f::Zero();
    Eigen::Quaternionf spawn_rotation = Eigen::Quaternionf::Identity();
    RelativeTo relative_to = RelativeTo::Head;
    switch (method) {
    case ToastDisplay::Hide: return nullptr;
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
            return nullptr;
        }
        size = {.width = title_size->width, .height = title_size->height + 20.0f};
    } else {
        const auto title_size = g_fonts->MeasureText(title, kFontSize);
        const auto body_size = g_fonts->MeasureText(toast.body, kFontSize);
        if (!title_size || !body_size) {
            return nullptr;
        }
        size = {.width = std::max(title_size->width, body_size->width), .height = body_size->height + 50.0f};
    }

    const float text_width = size.width;
    size.width += kPaddingX * 2.0f;

    auto form = std::make_unique<Form>(std::string(kToastName));
    form->SetSize({std::floor(size.width), std::floor(size.height)});

    auto& background = form->AddComponent<PanelComponent>("background");
    background.SetRect({0.0f, 0.0f, size.width, size.height});
    background.SetCornerRadius(16.0f);
    background.SetBgColor(Color("@panel"));

    if (toast.body.empty()) {
        auto& heading = form->AddComponent<LabelComponent>("title", title);
        heading.SetRect({kPaddingX, 0.0f, text_width, size.height});
        heading.SetFontSize(kFontSize);
        heading.SetFgColor(Color("@text"));
        heading.SetCentered(true);
    } else {
        auto& body = form->AddComponent<LabelComponent>("body", toast.body);
        body.SetRect({kPaddingX, 54.0f, text_width, size.height - 54.0f});
        body.SetFontSize(kFontSize);
        body.SetFgColor(Color("@text"));

        auto& header = form->AddComponent<PanelComponent>("header");
        header.SetRect({0.0f, 0.0f, size.width, 30.0f});
        header.SetCornerRadius(16.0f);
        header.SetBgColor(Color("@background"));

        auto& heading = header.AddComponent<LabelComponent>("title", title);
        heading.SetRect({kPaddingX, 16.0f, text_width, static_cast<float>(kFontSize) + 2.0f});
        heading.SetFontSize(kFontSize);
        heading.SetFgColor(Color("@soft-text"));
        heading.SetCentered(true);
    }

    auto created = Compose::Create(std::move(form));
    if (!created) {
        return nullptr;
    }

    auto state = std::unique_ptr<Toast>(new Toast());
    state->SetComposer(std::move(*created));
    state->SetName(std::string(kToastName));
    state->SetWantVisible(true);
    state->SetSpawnScale(size.width * kPixelsToMeters);
    state->SetSpawnRotation(spawn_rotation);
    state->SetSpawnPoint(spawn_point);
    state->SetZOrder(kZOrderToast);
    state->SetAttachedTo(relative_to);
    return state;
}

auto Toast::Show(const ToastMessage& toast) -> void {
    ShowAt(toast, std::chrono::steady_clock::now());
}

auto Toast::ShowAt(const ToastMessage& toast, std::chrono::steady_clock::time_point instant) -> void {
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
            .create = [toast, destroy_at]() -> std::unique_ptr<View> {
                auto created = Create(toast);
                if (created) {
                    created->AutoMovement();
                    g_tasks->EnqueueAt(DropOverlayTask{.selector = created->Id()}, destroy_at);
                }
                return created;
            },
        },
        instant
    );
}

auto Toast::ShowError(std::string_view message) -> void {
    LOG_ERROR("{}", message);
    Show(ToastMessage{.topic = ToastTopic::System, .title = "Error", .body = std::string(message), .timeout = 3.0f});
}
