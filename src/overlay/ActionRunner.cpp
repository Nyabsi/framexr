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

#include "ActionRunner.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <numbers>
#include <ranges>
#include <utility>

#include "AudioPlayback.hpp"
#include "Config.hpp"
#include "ConfigStore.hpp"
#include "Settings.hpp"
#include "FontCache.hpp"
#include "HidProvider.hpp"
#include "Json.hpp"
#include "Keyboard.hpp"
#include "KeyboardLayout.hpp"
#include "Keymap.hpp"
#include "Logger.hpp"
#include "Osc.hpp"
#include "Container.hpp"
#include "TaskQueue.hpp"
#include "Toast.hpp"
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

auto SubmitSystemToast(std::string title) -> void {
    Toast::Show(ToastMessage{.topic = ToastTopic::System, .title = std::move(title)});
}

auto WatchSelector() -> OverlaySelector {
    return std::string(kWatchName);
}

} // namespace

auto ActionRunner::PlayThump() -> void {
    g_audio->Play(std::as_bytes(std::span(kThumpSound)));
}

auto ActionRunner::PressShortcut(const std::vector<VirtualKey>& keys) -> void {
    for (const auto key : keys) {
        g_hid->SendKey(key, true);
    }
    for (const auto key : std::views::reverse(keys)) {
        g_hid->SendKey(key, false);
    }
}

auto ActionRunner::TypeText(std::string_view text) -> void {
    if (!g_interaction->TypingKeymap()) {
        LOG_WARN("Cannot type text without a keyboard layout");
        return;
    }
    for (const auto character : FontCache::DecodeUtf8(text)) {
        std::optional<TranslatedKey> typed;
        if (character == U'\n') {
            typed = TranslatedKey{.key = VirtualKey::Return};
        } else if (character == U'\t') {
            typed = TranslatedKey{.key = VirtualKey::Tab};
        } else {
            typed = g_interaction->TypingKeymap()->Find(static_cast<char32_t>(character));
        }
        if (!typed) {
            LOG_WARN("Cannot type U+{:04X} with the desktop layout", static_cast<uint32_t>(character));
            continue;
        }
        g_hid->SetModifiers(typed->modifiers);
        g_hid->SendKey(typed->key, true);
        g_hid->SendKey(typed->key, false);
    }
    g_hid->SetModifiers(0);
}

auto ActionRunner::Run(const std::vector<ButtonAction>& actions) -> void {
    for (const auto& action : actions) {
        Run(action);
    }
}

auto ActionRunner::Run(const ButtonAction& action) -> void {
    std::visit(
        [&](const auto& item) -> void {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, ExecButtonAction>) {
                RunExec(item);
            } else if constexpr (std::is_same_v<Item, WatchButtonAction>) {
                RunWatch(item.action);
            } else if constexpr (std::is_same_v<Item, OverlayButtonAction>) {
                RunOverlay(item.target, item.action);
            } else if constexpr (std::is_same_v<Item, WindowButtonAction>) {
                RunWindow(item.target, item.action);
            } else if constexpr (std::is_same_v<Item, VirtualKeyButtonAction>) {
                g_hid->SendKey(item.keycode, item.action == PressRelease::Press);
            } else if constexpr (std::is_same_v<Item, ToastButtonAction>) {
                Toast::Show(
                    ToastMessage{
                        .topic = ToastTopic::System,
                        .title = item.message,
                        .body = item.body.value_or(""),
                        .timeout = item.seconds.value_or(5.0f),
                    }
                );
            } else if constexpr (std::is_same_v<Item, ColorAdjustButtonAction>) {
                g_tasks->Enqueue(SystemTask{ColorGainTask{.channel = item.channel, .delta = item.delta}});
            } else if constexpr (std::is_same_v<Item, SystemButtonAction>) {
                RunSystem(item.action);
            } else if constexpr (std::is_same_v<Item, DragMultiplierButtonAction>) {
                g_config->space_drag_multiplier += item.delta;
            } else if constexpr (std::is_same_v<Item, ShortcutButtonAction>) {
                PressShortcut(item.keys);
            } else if constexpr (std::is_same_v<Item, TypeTextButtonAction>) {
                TypeText(item.text);
            } else if constexpr (std::is_same_v<Item, KeyboardProfileButtonAction>) {
                RunProfile(item.profile);
            } else if constexpr (std::is_same_v<Item, SendOscButtonAction>) {
#if FRAMEXR_FEATURE_OSC
                if (g_osc) {
                    if (!item.values) {
                        FatalError("SendOscValue requires values");
                    }
                    if (const auto sent = g_osc->SendMessage(item.parameter, *item.values); !sent) {
                        LOG_TRACE("Failed to send OSC value: {}", sent.error().Message());
                    }
                    PlayThump();
                }
#else
                Toast::ShowError("OSC feature is not enabled");
#endif
            }
        },
        action
    );
}

auto ActionRunner::RunProfile(const std::optional<std::string>& profile) -> void {
    const auto& next = profile ? KeyboardLayout::Find(*profile) : KeyboardLayout::After(g_config->keyboard_profile);
    Keyboard::SwitchProfile(g_interaction->TypingKeymap(), next.Name());
}

auto ActionRunner::RunExec(const ExecButtonAction& action) -> void {
    if (m_child) {
        const auto status = m_child->TryWait();
        if (!status) {
            m_child.reset();
            Toast::ShowError(std::format("Error checking child process: {}", status.error().Message()));
        } else if (!status->has_value()) {
            LOG_WARN("Unable to launch child process: previous child not exited yet");
            return;
        } else {
            if (!(*status)->Success()) {
                Toast::ShowError(std::format("Child process exited with code: {}", (*status)->Describe()));
            }
            m_child.reset();
        }
    }

    if (auto process = Process::Spawn(action.command)) {
        m_child.emplace(std::move(*process));
        if (action.toast) {
            SubmitSystemToast(*action.toast);
        }
    } else {
        Toast::ShowError(std::format("Failed to spawn process {}: {}", action.command, process.error().Message()));
    }
}

auto ActionRunner::RunSystem(SystemAction action) -> void {
    auto& config = *g_config;
    const auto state_name = [](bool value) -> std::string_view { return kEnabledDisabled[value ? 1 : 0]; };
    switch (action) {
    case SystemAction::ToggleAllowSliding:
        config.allow_sliding = !config.allow_sliding;
        SubmitSystemToast(std::format("Sliding is {}.", state_name(config.allow_sliding)));
        break;
    case SystemAction::Recenter: g_tasks->Enqueue(SystemTask{RecenterTask{}}); break;
    case SystemAction::ShowHide: g_tasks->Enqueue(SystemTask{ShowHideTask{}}); break;
    case SystemAction::ToggleRotateLock:
        config.space_rotate_unlocked = !config.space_rotate_unlocked;
        SubmitSystemToast(std::format("Space rotate axis lock now {}.", state_name(!config.space_rotate_unlocked)));
        break;
    case SystemAction::PlayspaceResetOffset: g_tasks->Enqueue(SystemTask{ResetPlayspaceTask{}}); break;
    case SystemAction::PlayspaceFixFloor: {
        const auto now = std::chrono::steady_clock::now();
        for (int i = 0; i < 5; ++i) {
            Toast::ShowAt(
                ToastMessage{
                    .topic = ToastTopic::System,
                    .title = std::format("Fixing floor in {}", 5 - i),
                    .body = "Place either controller on the floor.",
                    .timeout = 1.0f,
                },
                now + std::chrono::seconds(i)
            );
        }
        g_tasks->EnqueueAt(SystemTask{FixFloorTask{}}, now + std::chrono::seconds(5));
        break;
    }
    case SystemAction::RecalculateExtent: FatalError("not yet implemented"); break;
    case SystemAction::ToggleNotifications:
        config.notifications_enabled = !config.notifications_enabled;
        SubmitSystemToast(std::format("Notifications are {}.", state_name(config.notifications_enabled)));
        break;
    case SystemAction::ToggleNotificationSounds:
        config.notifications_sound_enabled = !config.notifications_sound_enabled;
        SubmitSystemToast(std::format("Notification sounds are {}.", state_name(config.notifications_sound_enabled)));
        break;
    case SystemAction::PersistConfig:
        if (const auto result = ConfigStore::SaveSettings(); !result) {
            Toast::ShowError(std::format("Failed to save config: {}", result.error().Message()));
        }
        break;
    case SystemAction::PersistLayout:
        if (const auto result = ConfigStore::SaveLayout(); !result) {
            Toast::ShowError(std::format("Failed to save layout: {}", result.error().Message()));
        }
        break;
    case SystemAction::Edit: g_tasks->Enqueue(SystemTask{OpenEditorTask{}}); break;
    }
}

auto ActionRunner::RunWatch(const WatchAction& action) -> void {
    std::visit(
        [&](const auto& item) -> void {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, WatchHide>) {
                g_tasks->Enqueue(
                    ModifyOverlayTask{
                        .selector = WatchSelector(),
                        .modify = [](View& watch) -> void {
                            if (!watch.SavedTransform()) {
                                watch.SetWantVisible(false);
                                watch.SetSavedTransform(watch.Transform());
                                Toast::Show(
                                    ToastMessage{
                                        .topic = ToastTopic::System,
                                        .title = "Watch hidden",
                                        .body = "Use show/hide binding to restore.",
                                        .timeout = 3.0f,
                                    }
                                );
                            } else {
                                watch.SetWantVisible(true);
                                watch.SavedTransform().reset();
                                SubmitSystemToast("Watch restored");
                            }
                        },
                    }
                );
                PlayThump();
            } else if constexpr (std::is_same_v<Item, WatchSwitchHands>) {
                g_tasks->Enqueue(
                    ModifyOverlayTask{
                        .selector = WatchSelector(),
                        .modify = [](View& watch) -> void {
                            if (watch.AttachedTo() == RelativeTo::LeftHand) {
                                watch.SetAttachedTo(RelativeTo::RightHand);
                                watch.SetSpawnRotation(
                                    g_config->watch_rot * Eigen::AngleAxisf(std::numbers::pi_v<float>, Eigen::Vector3f::UnitX())
                                    * Eigen::AngleAxisf(std::numbers::pi_v<float>, Eigen::Vector3f::UnitZ())
                                );
                                watch.SetSpawnPoint(g_config->watch_pos);
                                watch.SpawnPoint().x() *= -1.0f;
                            } else {
                                watch.SetAttachedTo(RelativeTo::LeftHand);
                                watch.SetSpawnRotation(g_config->watch_rot);
                                watch.SetSpawnPoint(g_config->watch_pos);
                            }
                            watch.SetDirty(true);
                            Toast::Show(
                                ToastMessage{
                                    .topic = ToastTopic::System,
                                    .title = "Watch switched",
                                    .body = "Check your other hand",
                                    .timeout = 3.0f,
                                }
                            );
                        },
                    }
                );
                PlayThump();
            } else if constexpr (std::is_same_v<Item, WatchViewAngle>) {
                auto& config = *g_config;
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
                g_tasks->Enqueue(
                    ModifyOverlayTask{
                        .selector = WatchSelector(),
                        .modify = [rotation](View& watch) -> void {
                            watch.SpawnRotation() *= rotation;
                            g_config->watch_rot = watch.SpawnRotation();
                            watch.SetDirty(true);
                        },
                    }
                );
            } else {
                const auto axis = static_cast<Eigen::Index>(item.axis);
                const float delta = item.delta;
                g_tasks->Enqueue(
                    ModifyOverlayTask{
                        .selector = WatchSelector(),
                        .modify = [axis, delta](View& watch) -> void {
                            watch.SpawnPoint()[axis] += delta;
                            g_config->watch_pos = watch.SpawnPoint();
                            watch.SetDirty(true);
                        },
                    }
                );
            }
        },
        action
    );
}

auto ActionRunner::RunOverlay(const OverlaySelector& target, const OverlayAction& action) -> void {
    std::visit(
        [&](const auto& item) -> void {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, OverlayToggleVisible>) {
                g_tasks->Enqueue(
                    ModifyOverlayTask{
                        .selector = target,
                        .modify = [](View& overlay) -> void {
                            overlay.SetWantVisible(!overlay.WantVisible());
                            if (overlay.Recenter()) {
                                overlay.SetShowHide(overlay.WantVisible());
                                overlay.Reset(false);
                            }
                            auto& screens = g_config->show_screens;
                            bool changed = false;
                            if (!overlay.WantVisible()) {
                                changed = std::erase(screens, overlay.Name()) > 0;
                            } else if (std::ranges::find(screens, overlay.Name()) == screens.end()) {
                                screens.push_back(overlay.Name());
                                changed = true;
                            }
                            if (changed) {
                                if (const auto result = ConfigStore::SaveLayout()) {
                                    LOG_DEBUG("Saved state");
                                } else {
                                    Toast::ShowError(std::format("Failed to save state: {}", result.error().Message()));
                                }
                            }
                        },
                    }
                );
            } else if constexpr (std::is_same_v<Item, OverlayToggleImmovable>) {
                g_tasks->Enqueue(
                    ModifyOverlayTask{
                        .selector = target,
                        .modify = [](View& overlay) -> void {
                            overlay.SetRecenter(!overlay.Recenter());
                            overlay.SetGrabbable(overlay.Recenter());
                            overlay.SetShowHide(overlay.Recenter());
                            SubmitSystemToast(
                                overlay.Recenter() ? std::format("{} is now unlocked!", overlay.Name())
                                                   : std::format("{} is now locked in place!", overlay.Name())
                            );
                        },
                    }
                );
                PlayThump();
            } else if constexpr (std::is_same_v<Item, OverlayToggleInteraction>) {
                g_tasks->Enqueue(
                    ModifyOverlayTask{
                        .selector = target,
                        .modify = [](View& overlay) -> void {
                            overlay.SetInteractable(!overlay.Interactable());
                            SubmitSystemToast(
                                overlay.Interactable() ? std::format("{} is now interactable!", overlay.Name())
                                                       : std::format("{} is now non-interactable!", overlay.Name())
                            );
                        },
                    }
                );
                PlayThump();
            } else {
                const float delta = item.delta;
                g_tasks->Enqueue(
                    ModifyOverlayTask{
                        .selector = target,
                        .modify = [delta](View& overlay) -> void {
                            overlay.SetAlpha(std::clamp(overlay.Alpha() + delta, 0.1f, 1.0f));
                            overlay.SetDirty(true);
                            LOG_DEBUG("{}: alpha {}", overlay.Name(), overlay.Alpha());
                        },
                    }
                );
            }
        },
        action
    );
}

auto ActionRunner::RunWindow(const std::string& target, WindowAction action) -> void {
    switch (action) {
    case WindowAction::ShowMirror:
#if FRAMEXR_FEATURE_WAYLAND
        g_tasks->Enqueue(
            CreateOverlayTask{
                .selector = target,
                .create = [target]() -> std::unique_ptr<View> {
                    Toast::Show(ToastMessage{.topic = ToastTopic::System, .title = "Check your desktop for popup.", .sound = true});
                    return std::make_unique<Mirror>(target, false);
                },
            }
        );
#else
        LOG_WARN("Mirror not available without Wayland feature.");
#endif
        break;
    case WindowAction::ShowUi:
        g_tasks->Enqueue(
            CreateOverlayTask{
                .selector = target,
                .create = [target]() -> std::unique_ptr<View> { return Settings::Create(target); },
            }
        );
        break;
    case WindowAction::Destroy: g_tasks->Enqueue(DropOverlayTask{.selector = target}); break;
    }
}
