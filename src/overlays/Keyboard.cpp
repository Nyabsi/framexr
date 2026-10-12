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

#include "Keyboard.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <clocale>
#include <cmath>
#include <cwctype>
#include <map>
#include <utility>

#include <locale.h>

#include "ActionKeyControl.hpp"
#include "AudioOutput.hpp"
#include "Button.hpp"
#include "CommandBatch.hpp"
#include "Config.hpp"
#include "Device.hpp"
#include "Font.hpp"
#include "Hid.hpp"
#include "InteractionHandler.hpp"
#include "KeyboardBackend.hpp"
#include "KeyboardProfiles.hpp"
#include "KeyControl.hpp"
#include "Logger.hpp"
#include "ModifierControl.hpp"
#include "OverlayContainer.hpp"
#include "OverlayRenderer.hpp"
#include "ProfileControl.hpp"
#include "Task.hpp"
#include "TextInput.hpp"
#include "Theme.hpp"
#include "framexr_config.h"

namespace {

constexpr float kButtonPadding = 7.0f;

constexpr int kKeyFontSize = 15;

constexpr float kMetersPerUnit = 0.04f;

constexpr float kKeyboardDrop = 0.5f;

constexpr float kKeyboardReach = 0.8f;

constexpr auto kLatchingKeys = std::to_array<VirtualKey>({VirtualKey::Caps, VirtualKey::NumLock, VirtualKey::Scroll});

constexpr unsigned char kKeyClickSound[] = {
#embed "421581.wav"
};

auto Color(std::string_view hex) -> Eigen::Vector4f {
    return Canvas::ParseColor(hex).value_or(Canvas::FallbackColor());
}

auto RegionKeymap(const std::string& name) -> std::shared_ptr<const Keymap> {
    static std::map<std::string, std::shared_ptr<const Keymap>, std::less<>> loaded;
    if (const auto found = loaded.find(name); found != loaded.end()) {
        return found->second;
    }
    std::shared_ptr<const Keymap> keymap;
    const auto region = KeyTranslator::ParseRegion(name);
    if (auto created = Keymap::FromNames(region.layout, region.variant)) {
        keymap = std::make_shared<const Keymap>(std::move(*created));
    } else {
        LOG_ERROR("Keyboard region {}: {}", name, created.error().Message());
    }
    loaded.emplace(name, keymap);
    return keymap;
}

auto StartsWithLetter(std::string_view text) -> bool {
    const auto codepoints = FontCache::DecodeUtf8(text);
    if (codepoints.empty()) {
        return false;
    }
    const uint32_t first = codepoints.front();
    if (first < 0x80) {
        return std::isalpha(static_cast<int>(first)) != 0;
    }
#ifdef _WIN32
    return first <= 0xFFFF && iswalpha(static_cast<wint_t>(first)) != 0;
#else
    static const locale_t utf8_locale = newlocale(LC_CTYPE_MASK, "C.UTF-8", nullptr);
    return utf8_locale != nullptr && iswalpha_l(static_cast<wint_t>(first), utf8_locale) != 0;
#endif
}

} // namespace

auto Keyboard::IsLatchingKey(VirtualKey key) -> bool {
    return std::ranges::contains(kLatchingKeys, key);
}

auto Keyboard::SendKey(VirtualKey key, bool down) -> void {
    g_hid->SendKey(key, down);
}

auto Keyboard::SetModifiers(uint8_t modifiers) -> void {
    g_hid->SetModifiers(modifiers);
}

auto Keyboard::Click() -> void {
    if (g_config->keyboard_sound_enabled) {
        g_audio->Play(std::as_bytes(std::span(kKeyClickSound)));
    }
}

auto Keyboard::CanvasSize(const KeyboardLayout& layout) -> Eigen::Vector2f {
    return {
        std::fma(layout.Columns(), kPixelsPerUnit, 2.0f * kKeyboardMargin),
        std::fma(static_cast<float>(layout.Rows()), kPixelsPerUnit, 2.0f * kKeyboardMargin),
    };
}

auto Keyboard::KeyRect(const KeyDefinition& key, const KeyboardPlacement& placement) -> CanvasRect {
    const float scale = placement.scale;
    return {
        placement.origin.x() + std::fma(kPixelsPerUnit, key.x, kKeyboardMargin + kButtonPadding) * scale,
        placement.origin.y() + std::fma(kPixelsPerUnit, static_cast<float>(key.row), kKeyboardMargin + kButtonPadding) * scale,
        std::fma(kPixelsPerUnit, key.width, -(2.0f * kButtonPadding)) * scale,
        std::fma(-2.0f, kButtonPadding, kPixelsPerUnit) * scale,
    };
}

auto Keyboard::Fill(
    Canvas& canvas,
    const KeyboardLayout& layout,
    const std::shared_ptr<const Keymap>& keymap,
    const std::shared_ptr<KeyboardState>& state,
    const KeyboardPlacement& placement
) -> void {
    const float scale = placement.scale;
    const Eigen::Vector2f size = CanvasSize(layout) * scale;
    const auto rounding = g_themes->Active().rounding;
    canvas.SetBgColor(Color("@shadow"));
    canvas.Panel(placement.origin.x(), placement.origin.y(), size.x(), size.y(), Theme::Radius(rounding, Shape::Panel) * scale);
    canvas.SetFontSize(static_cast<int>(std::lround(static_cast<float>(kKeyFontSize) * scale)));
    const Eigen::Vector4f fg_color = Color("@text");
    const Eigen::Vector4f bg_color = Color("@panel");
    state->SetAltModifier(layout.AltModifierMask());

    std::shared_ptr<const Keymap> region_keymap;
    if (const auto region_name = layout.Region().value_or(g_config->keyboard_region); !region_name.empty()) {
        region_keymap = RegionKeymap(region_name);
        if (region_keymap && keymap && placement.interactive) {
            state->SetTranslator(std::make_shared<const KeyTranslator>(KeyTranslator::Create(region_keymap, keymap)));
        }
    }
    state->SetTypingKeymap(region_keymap ? region_keymap : keymap);
    const Keymap* typing_keymap = state->TypingKeymap().get();
    const bool has_altgr = typing_keymap != nullptr && typing_keymap->HasAltGr();
    const Keymap* label_keymap = layout.AutoLabels().value_or(true) ? typing_keymap : nullptr;

    for (const auto& key : layout.Keys()) {
        const auto rect = KeyRect(key, placement);
        std::vector<std::string> labels;
        std::unique_ptr<Control> control;
        auto cap_type = KeyCapType::Regular;
        std::optional<std::string> upper_case;

        if (std::holds_alternative<ProfileKey>(key.action)) {
            control = std::make_unique<ProfileControl>(keymap, layout.Name());
        } else if (const auto* action = std::get_if<ActionKey>(&key.action)) {
            control = std::make_unique<ActionKeyControl>(*action);
        } else if (const auto* typed = std::get_if<TypeKey>(&key.action)) {
            const auto virtual_key = typed->key;
            if (label_keymap != nullptr && !key.label) {
                switch (VirtualKeys::Type(virtual_key)) {
                case KeyType::Symbol: {
                    auto label0 = label_keymap->LabelForKey(virtual_key, 0);
                    auto label1 = label_keymap->LabelForKey(virtual_key, kModShift);
                    if (StartsWithLetter(label0)) {
                        labels.push_back(std::move(label0));
                        upper_case = std::move(label1);
                        if (has_altgr) {
                            cap_type = KeyCapType::RegularAltGr;
                            labels.push_back(label_keymap->LabelForKey(virtual_key, kModMeta));
                        }
                    } else {
                        labels.push_back(std::move(label0));
                        labels.push_back(std::move(label1));
                        if (has_altgr) {
                            labels.push_back(label_keymap->LabelForKey(virtual_key, kModMeta));
                            cap_type = KeyCapType::ReversedAltGr;
                        } else {
                            cap_type = KeyCapType::Reversed;
                        }
                    }
                    break;
                }
                case KeyType::NumPad: labels.push_back(label_keymap->LabelForKey(virtual_key, kModNumLock)); break;
                case KeyType::Other: break;
                }
            }
            if (const auto modifier = VirtualKeys::ModifierOf(virtual_key); modifier && !IsLatchingKey(virtual_key)) {
                control = std::make_unique<ModifierControl>(state, *modifier);
            } else {
                control = std::make_unique<KeyControl>(state, virtual_key);
            }
        }
        if (!placement.interactive) {
            control = std::make_unique<Control>();
        }

        if (labels.empty()) {
            labels = layout.Labels(key);
            if (labels.size() == 1 && labels.front().size() == 1 && StartsWithLetter(labels.front())) {
                upper_case = std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(labels.front().front()))));
            }
        }
        if (key.shifted_label) {
            upper_case = *key.shifted_label;
        }
        canvas.SetFgColor(key.style.fg_color ? Color(*key.style.fg_color) : fg_color);
        canvas.SetBgColor(key.style.bg_color ? Color(*key.style.bg_color) : bg_color);
        const size_t first_label = canvas.ControlCount() + 1;
        canvas.KeyButton(
            rect.x,
            rect.y,
            rect.w,
            rect.h,
            Theme::Resolve(key.style.corner_radius, rounding, Shape::Key) * scale,
            cap_type,
            labels,
            std::move(control)
        );
        if (upper_case && !labels.empty() && first_label < canvas.ControlCount()) {
            state->Letters().push_back(
                {.label = &canvas.ControlAt(first_label), .lower = labels.front(), .upper = std::move(*upper_case)}
            );
        }
    }
}

auto Keyboard::Create(const std::shared_ptr<const Keymap>& keymap) -> Result<std::unique_ptr<Overlay>> {
    return CreateFrom(keymap, g_keyboard_profiles->Find(g_config->keyboard_profile));
}

auto Keyboard::CreateFrom(const std::shared_ptr<const Keymap>& keymap, const KeyboardLayout& layout)
    -> Result<std::unique_ptr<Overlay>> {
    const auto size = CanvasSize(layout);
    const float width = size.x();
    const float height = size.y();
    auto state = std::make_shared<KeyboardState>();
    ASSIGN_OR_RETURN(
        auto canvas,
        Canvas::Create(static_cast<uint32_t>(width), static_cast<uint32_t>(height), g_device->NativeFormat())
    );
    Fill(*canvas, layout, keymap, state, {});

    OverlayState overlay;
    overlay.SetName(std::string(kKeyboardName));
    overlay.SetGrabbable(true);
    overlay.SetRecenter(true);
    overlay.SetInteractable(true);
    overlay.SetSpawnScale(width / kPixelsPerUnit * kMetersPerUnit * g_config->keyboard_scale);
    overlay.SetSpawnPoint(Eigen::Vector3f(0.0f, -kKeyboardDrop, -kKeyboardReach));
    overlay.SetSpawnRotation(
        Eigen::Quaternionf(Eigen::AngleAxisf(-std::atan2(kKeyboardDrop, kKeyboardReach), Eigen::Vector3f::UnitX()))
    );
    overlay.SetInteractionTransform(Eigen::Translation2f(0.5f, 0.5f) * Eigen::Scaling(1.0f, -width / height));

    return std::make_unique<Overlay>(std::move(overlay), std::make_unique<KeyboardBackend>(std::move(canvas), std::move(state)));
}

auto Keyboard::SwitchProfile(std::shared_ptr<const Keymap> keymap, std::string profile) -> void {
    LOG_INFO("Switching keyboard profile to {}", profile);
    g_config->keyboard_profile = std::move(profile);
    g_tasks->Enqueue(
        ModifyOverlayTask{
            .selector = std::string(kKeyboardName),
            .modify = [keymap = std::move(keymap)](OverlayState& previous) -> void {
                g_tasks->Enqueue(DropOverlayTask{.selector = std::string(kKeyboardName)});
                g_tasks->Enqueue(
                    CreateOverlayTask{
                        .selector = std::string(kKeyboardName),
                        .create = [keymap, previous]() -> std::optional<std::pair<OverlayState, std::unique_ptr<OverlayBackend>>> {
                            auto created = Keyboard::Create(keymap);
                            if (!created) {
                                LOG_ERROR("Could not create keyboard: {}", created.error().Message());
                                return std::nullopt;
                            }
                            auto& overlay = **created;
                            auto& next = overlay.State();
                            const float factor = previous.SpawnScale() > 0.0f ? next.SpawnScale() / previous.SpawnScale() : 1.0f;
                            const auto rescale = [factor](Eigen::Affine3f transform) -> Eigen::Affine3f {
                                transform.linear() *= factor;
                                return transform;
                            };
                            next.SetWantVisible(previous.WantVisible());
                            next.SetShowHide(previous.ShowHide());
                            next.SetRecenter(previous.Recenter());
                            next.SetGrabbable(previous.Grabbable());
                            next.SetInteractable(previous.Interactable());
                            next.SetAlpha(previous.Alpha());
                            next.SetCurvature(previous.Curvature());
                            next.SetTransform(rescale(previous.Transform()));
                            if (previous.SavedTransform()) {
                                next.SetSavedTransform(rescale(*previous.SavedTransform()));
                                for (auto& [name, saved] : g_config->transform_values) {
                                    if (name == kKeyboardName) {
                                        saved = *next.SavedTransform();
                                    }
                                }
                            }
                            next.SetDirty(true);
                            return std::pair{std::move(next), overlay.TakeBackend()};
                        },
                    }
                );
            },
        }
    );
}
