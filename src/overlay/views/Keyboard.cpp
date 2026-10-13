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

#include "ActionRunner.hpp"
#include "KeyboardPicker.hpp"
#include "AudioPlayback.hpp"
#include "Compose.hpp"
#include "Config.hpp"
#include "FontCache.hpp"
#include "Form.hpp"
#include "FormTheme.hpp"
#include "HidProvider.hpp"
#include "KeyboardLayout.hpp"
#include "Logger.hpp"
#include "Container.hpp"
#include "PanelComponent.hpp"
#include "TaskQueue.hpp"
#include "TextInput.hpp"
#include "framexr_config.h"

namespace {

constexpr float kButtonPadding = 7.0f;

constexpr int kKeyFontSize = 15;

constexpr float kMetersPerUnit = 0.04f;

constexpr float kKeyboardDrop = 0.5f;

constexpr float kKeyboardReach = 0.8f;

constexpr unsigned char kKeyClickSound[] = {
#embed "421581.wav"
};

auto Click() -> void {
    if (g_config->keyboard_sound_enabled) {
        g_audio->Play(std::as_bytes(std::span(kKeyClickSound)));
    }
}

auto ActiveColor() -> Eigen::Vector4f {
    return {198.0f / 255.0f, 160.0f / 255.0f, 246.0f / 255.0f, 0.5f};
}

auto LatchedColor() -> Eigen::Vector4f {
    return {1.0f, 1.0f, 1.0f, 0.16f};
}

auto LatchedOutline() -> HighlightOutline {
    return {.color = {198.0f / 255.0f, 160.0f / 255.0f, 246.0f / 255.0f, 1.0f}, .width = 3.0f};
}

auto Color(std::string_view hex) -> Eigen::Vector4f {
    return FormTheme::ParseColor(hex).value_or(FormTheme::FallbackColor());
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
    FormComponent& parent,
    const KeyboardLayout& layout,
    const std::shared_ptr<const Keymap>& keymap,
    const KeyboardPlacement& placement,
    Keyboard* owner
) -> void {
    auto& board = parent.AddComponent<FormComponent>("keyboard");
    board.SetPosition(placement.origin);
    board.SetScale(placement.scale);
    const Eigen::Vector2f size = CanvasSize(layout);
    auto& background = board.AddComponent<PanelComponent>();
    background.SetSize(size);
    background.SetBgColor(Color("@shadow"));
    const Eigen::Vector4f fg_color = Color("@text");
    const Eigen::Vector4f bg_color = Color("@panel");
    std::shared_ptr<const Keymap> region_keymap;
    if (const auto region_name = layout.Region().value_or(g_config->keyboard_region); !region_name.empty()) {
        region_keymap = RegionKeymap(region_name);
    }
    const auto typing = region_keymap ? region_keymap : keymap;
    if (owner != nullptr) {
        auto& input = owner->m_input;
        input.SetAltModifier(layout.AltModifierMask());
        input.SetTypingKeymap(typing);
        input.SetTranslator(
            region_keymap && keymap ? std::make_shared<const KeyTranslator>(KeyTranslator::Create(region_keymap, keymap)) : nullptr
        );
        owner->m_letters.clear();
    }
    const Keymap* typing_keymap = typing.get();
    const bool has_altgr = typing_keymap != nullptr && typing_keymap->HasAltGr();
    const Keymap* label_keymap = layout.AutoLabels().value_or(true) ? typing_keymap : nullptr;

    for (const auto& key : layout.Keys()) {
        const auto rect = KeyRect(key, {});
        std::vector<std::string> labels;
        auto& placed = board.AddComponent<KeyComponent>();
        auto cap_type = KeyCapType::Regular;
        std::optional<std::string> upper_case;

        if (owner != nullptr) {
            owner->Bind(placed, key, keymap);
        }
        if (const auto* typed = std::get_if<TypeKey>(&key.action)) {
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
        }
        placed.SetInteractive(owner != nullptr);

        if (labels.empty()) {
            labels = layout.Labels(key);
            if (labels.size() == 1 && labels.front().size() == 1 && StartsWithLetter(labels.front())) {
                upper_case = std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(labels.front().front()))));
            }
        }
        if (key.shifted_label) {
            upper_case = *key.shifted_label;
        }
        placed.SetRect(rect);
        placed.SetCornerRadius(key.style.corner_radius);
        placed.SetFontSize(kKeyFontSize);
        placed.SetFgColor(key.style.fg_color ? Color(*key.style.fg_color) : fg_color);
        placed.SetBgColor(key.style.bg_color ? Color(*key.style.bg_color) : bg_color);
        placed.SetLegends(cap_type, labels);
        if (owner != nullptr && upper_case && !placed.Legends().empty()) {
            owner->m_letters.push_back(
                {.label = placed.Legends().front(), .lower = labels.front(), .upper = std::move(*upper_case)}
            );
        }
    }
}

Keyboard::Keyboard() {
    SetName(std::string(kKeyboardName));
    SetGrabbable(true);
    SetRecenter(true);
    SetInteractable(true);
    SetSpawnPoint(Eigen::Vector3f(0.0f, -kKeyboardDrop, -kKeyboardReach));
    SetSpawnRotation(Eigen::Quaternionf(Eigen::AngleAxisf(-std::atan2(kKeyboardDrop, kKeyboardReach), Eigen::Vector3f::UnitX())));
}

auto Keyboard::Create(const std::shared_ptr<const Keymap>& keymap) -> Result<std::unique_ptr<Keyboard>> {
    auto keyboard = std::unique_ptr<Keyboard>(new Keyboard());
    RETURN_IF_ERROR(keyboard->Apply(keymap, KeyboardLayout::Find(g_config->keyboard_profile)));
    return keyboard;
}

auto Keyboard::Apply(const std::shared_ptr<const Keymap>& keymap, const KeyboardLayout& layout) -> Result<> {
    const auto size = CanvasSize(layout);
    auto form = std::make_unique<Form>(std::string(kKeyboardName));
    form->SetSize(size.array().floor().matrix());
    Fill(*form, layout, keymap, {}, this);
    m_uppercase = false;
    ASSIGN_OR_RETURN(auto canvas, Compose::Create(std::move(form)));
    SetComposer(std::move(canvas));
    SetSpawnScale(size.x() / kPixelsPerUnit * kMetersPerUnit * g_config->keyboard_scale);
    SetInteractionTransform(Eigen::Translation2f(0.5f, 0.5f) * Eigen::Scaling(1.0f, -size.x() / size.y()));
    SetDirty(true);
    return {};
}

auto Keyboard::Update() -> void {
    if (!WantVisible()) {
        m_input.Reset();
    }
    if (m_input.Uppercase() != m_uppercase) {
        m_uppercase = m_input.Uppercase();
        for (const auto& letter : m_letters) {
            letter.label->SetText(m_uppercase ? letter.upper : letter.lower);
        }
    }
}

auto Keyboard::Bind(KeyComponent& key, const KeyDefinition& definition, const std::shared_ptr<const Keymap>& keymap) -> void {
    if (std::holds_alternative<ProfileKey>(definition.action)) {
        key.SetOnPress([keymap](PointerMode) -> void {
            Click();
            KeyboardPicker::Open(keymap);
        });
    } else if (const auto* action = std::get_if<ActionKey>(&definition.action)) {
        auto runner = std::make_shared<ActionRunner>();
        key.SetOnPress([runner, actions = action->press](PointerMode) -> void {
            Click();
            runner->Run(actions);
        });
        key.SetOnRelease([runner, actions = action->release]() -> void { runner->Run(actions); });
    } else if (const auto* typed = std::get_if<TypeKey>(&definition.action)) {
        const auto virtual_key = typed->key;
        key.SetOnPress([this, virtual_key](PointerMode mode) -> void {
            Click();
            m_input.Press(virtual_key, mode);
        });
        key.SetOnRelease([this, virtual_key]() -> void { m_input.Release(virtual_key); });
        if (const auto modifier = VirtualKeys::ModifierOf(virtual_key); modifier && !VirtualKeys::Latches(virtual_key)) {
            key.SetHighlightColor([this, bit = *modifier]() -> std::optional<Eigen::Vector4f> {
                return m_input.Holds(bit) ? std::optional(ActiveColor()) : std::nullopt;
            });
        } else {
            key.SetHighlightColor([this, virtual_key]() -> std::optional<Eigen::Vector4f> {
                return m_input.Latched(virtual_key) ? std::optional(LatchedColor()) : std::nullopt;
            });
            key.SetOutline([this, virtual_key]() -> std::optional<HighlightOutline> {
                return m_input.Latched(virtual_key) ? std::optional(LatchedOutline()) : std::nullopt;
            });
        }
    }
}

auto Keyboard::SwitchProfile(std::shared_ptr<const Keymap> keymap, std::string profile) -> void {
    LOG_INFO("Switching keyboard profile to {}", profile);
    g_config->keyboard_profile = std::move(profile);
    g_tasks->Enqueue(
        ModifyOverlayTask{
            .selector = std::string(kKeyboardName),
            .modify = [keymap = std::move(keymap)](View& view) -> void {
                auto& keyboard = dynamic_cast<Keyboard&>(view);
                const float before = keyboard.SpawnScale();
                if (auto applied = keyboard.Apply(keymap, KeyboardLayout::Find(g_config->keyboard_profile)); !applied) {
                    LOG_ERROR("Could not create keyboard: {}", applied.error().Message());
                    return;
                }
                const float factor = before > 0.0f ? keyboard.SpawnScale() / before : 1.0f;
                keyboard.Transform().linear() *= factor;
                if (auto& saved = keyboard.SavedTransform()) {
                    saved->linear() *= factor;
                    for (auto& [name, transform] : g_config->transform_values) {
                        if (name == kKeyboardName) {
                            transform = *saved;
                        }
                    }
                }
            },
        }
    );
}
