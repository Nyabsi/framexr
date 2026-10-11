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
#include <cmath>
#include <map>
#include <utility>

#include <clocale>
#include <cwctype>
#include <locale.h>

#include "Button.hpp"
#include "Font.hpp"
#include "Hid.hpp"
#include "Logger.hpp"
#include "State.hpp"
#include "Theme.hpp"
#include "framexr_config.h"

namespace {

constexpr float kButtonPadding = 7.0f;
constexpr int kKeyFontSize = 15;
constexpr float kMetersPerUnit = 0.04f;
constexpr float kKeyboardDrop = 0.5f;
constexpr float kKeyboardReach = 0.8f;
constexpr std::array<uint8_t, 5> kAutoReleaseModifiers{kModShift, kModCtrl, kModAlt, kModSuper, kModMeta};

constexpr auto kLatchingKeys = std::to_array<VirtualKey>({VirtualKey::Caps, VirtualKey::NumLock, VirtualKey::Scroll});
constexpr float kLatchedOutlineWidth = 3.0f;

auto ActiveColor() -> Eigen::Vector4f {
    return {198.0f / 255.0f, 160.0f / 255.0f, 246.0f / 255.0f, 0.5f};
}

auto LatchedColor() -> Eigen::Vector4f {
    return {1.0f, 1.0f, 1.0f, 0.16f};
}

auto LatchedOutline() -> HighlightOutline {
    return {.color = {198.0f / 255.0f, 160.0f / 255.0f, 246.0f / 255.0f, 1.0f}, .width = kLatchedOutlineWidth};
}

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
    const auto region = KeyboardRegion::Parse(name);
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
    static const locale_t utf8_locale = newlocale(LC_CTYPE_MASK, "C.UTF-8", nullptr);
    return utf8_locale != nullptr && iswalpha_l(static_cast<wint_t>(first), utf8_locale) != 0;
}

} // namespace

auto KeyboardState::RefreshLetterCase() -> void {
    const bool upper = caps_lock != ((modifiers & kModShift) != 0);
    for (const auto& letter : letters) {
        letter.label->SetText(upper ? letter.upper : letter.lower);
    }
}

KeyControl::KeyControl(std::shared_ptr<KeyboardState> state, VirtualKey key)
    : state_(std::move(state)),
      key_(key),
      sent_(key),
      latches_(Keyboard::IsLatchingKey(key)) {
}

auto KeyControl::OnPress(AppState& app, PointerMode mode) -> void {
    Keyboard::Click(app);
    if (mode == PointerMode::Right) {
        state_->modifiers |= kModShift;
    } else if (mode == PointerMode::Middle) {
        state_->modifiers |= state_->alt_modifier;
    }
    sent_ = key_;
    translated_ = false;
    uint8_t modifiers = state_->modifiers;
    if (state_->translator && !latches_) {
        if (const auto target = state_->translator->Translate(key_, state_->modifiers, state_->caps_lock)) {
            sent_ = target->key;
            translated_ = true;
            modifiers = static_cast<uint8_t>((state_->modifiers & ~(kModShift | kModMeta)) | target->modifiers);
        }
    }
    app.hid->SetModifiers(modifiers);
    Keyboard::SendKey(app, sent_, true);
    if (latches_) {
        latched_ = !latched_;
        if (key_ == VirtualKey::Caps) {
            state_->caps_lock = latched_;
        }
    }
}

auto KeyControl::OnRelease(AppState& app) -> void {
    Keyboard::SendKey(app, sent_, false);
    if (translated_) {
        Keyboard::SetModifiers(app, state_->modifiers);
    }
    for (const uint8_t modifier : kAutoReleaseModifiers) {
        if ((state_->modifiers & modifier) != 0) {
            state_->modifiers &= static_cast<uint8_t>(~modifier);
            Keyboard::SetModifiers(app, state_->modifiers);
        }
    }
    state_->RefreshLetterCase();
}

auto KeyControl::HighlightColor(AppState&) -> std::optional<Eigen::Vector4f> {
    return latched_ ? std::optional(LatchedColor()) : std::nullopt;
}

auto KeyControl::Outline(AppState&) -> std::optional<HighlightOutline> {
    return latched_ ? std::optional(LatchedOutline()) : std::nullopt;
}

ModifierControl::ModifierControl(std::shared_ptr<KeyboardState> state, uint8_t modifier)
    : state_(std::move(state)),
      modifier_(modifier) {
}

auto ModifierControl::OnPress(AppState& app, PointerMode) -> void {
    sticky_ = (state_->modifiers & modifier_) == 0;
    state_->modifiers |= modifier_;
    Keyboard::Click(app);
    Keyboard::SetModifiers(app, state_->modifiers);
    state_->RefreshLetterCase();
}

auto ModifierControl::OnRelease(AppState& app) -> void {
    if (!sticky_) {
        state_->modifiers &= static_cast<uint8_t>(~modifier_);
        Keyboard::SetModifiers(app, state_->modifiers);
        state_->RefreshLetterCase();
    }
}

auto ModifierControl::HighlightColor(AppState&) -> std::optional<Eigen::Vector4f> {
    return (state_->modifiers & modifier_) != 0 ? std::optional(ActiveColor()) : std::nullopt;
}

ActionKeyControl::ActionKeyControl(ActionKey action) : action_(std::move(action)) {
}

auto ActionKeyControl::OnPress(AppState& app, PointerMode) -> void {
    Keyboard::Click(app);
    press_runner_.Run(app, action_.press);
}

auto ActionKeyControl::OnRelease(AppState& app) -> void {
    release_runner_.Run(app, action_.release);
}

ProfileControl::ProfileControl(std::shared_ptr<const Keymap> keymap, std::string current)
    : keymap_(std::move(keymap)),
      current_(std::move(current)) {
}

auto ProfileControl::OnPress(AppState& app, PointerMode) -> void {
    Keyboard::Click(app);
    Keyboard::SwitchProfile(app, keymap_, KeyboardProfiles::After(current_).name);
}

KeyboardBackend::KeyboardBackend(std::unique_ptr<Canvas> canvas, std::shared_ptr<KeyboardState> state)
    : canvas_(std::move(canvas)),
      state_(std::move(state)) {
}

auto KeyboardBackend::SetRenderer(std::unique_ptr<OverlayRenderer> renderer) -> void {
    canvas_->SetRenderer(std::move(renderer));
}

auto KeyboardBackend::SetInteraction(std::unique_ptr<InteractionHandler> interaction) -> void {
    canvas_->SetInteraction(std::move(interaction));
}

auto KeyboardBackend::Init(AppState& app) -> Result<> {
    return canvas_->Init(app);
}

auto KeyboardBackend::Pause(AppState& app) -> Result<> {
    state_->modifiers = 0;
    state_->RefreshLetterCase();
    Keyboard::SetModifiers(app, 0);
    return canvas_->Pause(app);
}

auto KeyboardBackend::Resume(AppState& app) -> Result<> {
    return canvas_->Resume(app);
}

auto KeyboardBackend::ShouldRender(AppState& app) -> Result<RenderReadiness> {
    return canvas_->ShouldRender(app);
}

auto KeyboardBackend::Render(AppState& app, const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha)
    -> Result<bool> {
    return canvas_->Render(app, target, batch, alpha);
}

auto KeyboardBackend::Meta() -> std::optional<FrameMeta> {
    return canvas_->Meta();
}

auto KeyboardBackend::OnHover(AppState& app, const PointerHit& hit) -> std::optional<Haptics> {
    return canvas_->OnHover(app, hit);
}

auto KeyboardBackend::OnLeft(AppState& app, size_t pointer) -> void {
    canvas_->OnLeft(app, pointer);
}

auto KeyboardBackend::OnPointer(AppState& app, const PointerHit& hit, bool pressed) -> void {
    canvas_->OnPointer(app, hit, pressed);
}

auto KeyboardBackend::OnScroll(AppState& app, const PointerHit& hit, float delta_y, float delta_x) -> void {
    canvas_->OnScroll(app, hit, delta_y, delta_x);
}

auto Keyboard::IsLatchingKey(VirtualKey key) -> bool {
    return std::ranges::contains(kLatchingKeys, key);
}

auto Keyboard::SendKey(AppState& app, VirtualKey key, bool down) -> void {
    app.hid->SendKey(key, down);
}

auto Keyboard::SetModifiers(AppState& app, uint8_t modifiers) -> void {
    app.hid->SetModifiers(modifiers);
}

auto Keyboard::Click(AppState& app) -> void {
    if (app.config.keyboard_sound_enabled) {
        app.audio.Play(std::as_bytes(std::span(kKeyClickSound)));
    }
}

auto Keyboard::CanvasSize(const KeyboardLayout& layout) -> Eigen::Vector2f {
    return {
        std::fma(layout.columns, kPixelsPerUnit, 2.0f * kKeyboardMargin),
        std::fma(static_cast<float>(layout.rows), kPixelsPerUnit, 2.0f * kKeyboardMargin),
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
    AppState& app,
    const std::shared_ptr<const Keymap>& keymap,
    const std::shared_ptr<KeyboardState>& state,
    const KeyboardPlacement& placement
) -> void {
    const float scale = placement.scale;
    const Eigen::Vector2f size = CanvasSize(layout) * scale;
    const auto rounding = Theme::Active().rounding;
    canvas.bg_color = Color("@shadow");
    canvas.Panel(placement.origin.x(), placement.origin.y(), size.x(), size.y(), Theme::Radius(rounding, Shape::Panel) * scale);
    canvas.font_size = static_cast<int>(std::lround(static_cast<float>(kKeyFontSize) * scale));
    const Eigen::Vector4f fg_color = Color("@text");
    const Eigen::Vector4f bg_color = Color("@panel");
    state->alt_modifier = layout.AltModifierMask();

    std::shared_ptr<const Keymap> region_keymap;
    if (const auto region_name = layout.region.value_or(app.config.keyboard_region); !region_name.empty()) {
        region_keymap = RegionKeymap(region_name);
        if (region_keymap && keymap && placement.interactive) {
            state->translator = std::make_shared<const KeyTranslator>(KeyTranslator::Create(region_keymap, keymap));
        }
    }
    const Keymap* typing_keymap = region_keymap ? region_keymap.get() : keymap.get();
    const bool has_altgr = typing_keymap != nullptr && typing_keymap->HasAltGr();
    const Keymap* label_keymap = layout.auto_labels.value_or(true) ? typing_keymap : nullptr;

    for (const auto& key : layout.keys) {
        const auto rect = KeyRect(key, placement);
        std::vector<std::string> labels;
        std::unique_ptr<Control> control;
        auto cap_type = KeyCapType::Regular;
        std::optional<std::string> upper_case;

        if (std::holds_alternative<ProfileKey>(key.action)) {
            control = std::make_unique<ProfileControl>(keymap, layout.name);
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
        canvas.fg_color = key.style.fg_color ? Color(*key.style.fg_color) : fg_color;
        canvas.bg_color = key.style.bg_color ? Color(*key.style.bg_color) : bg_color;
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
            state->letters.push_back(
                {.label = &canvas.ControlAt(first_label), .lower = labels.front(), .upper = std::move(*upper_case)}
            );
        }
    }
}

auto Keyboard::Create(AppState& app, const std::shared_ptr<const Keymap>& keymap) -> Result<std::unique_ptr<Overlay>> {
    return CreateFrom(app, keymap, KeyboardProfiles::Find(app.config.keyboard_profile));
}

auto Keyboard::CreateFrom(AppState& app, const std::shared_ptr<const Keymap>& keymap, const KeyboardLayout& layout)
    -> Result<std::unique_ptr<Overlay>> {
    const auto size = CanvasSize(layout);
    const float width = size.x();
    const float height = size.y();
    auto state = std::make_shared<KeyboardState>();
    ASSIGN_OR_RETURN(
        auto canvas,
        Canvas::Create(app.device, static_cast<uint32_t>(width), static_cast<uint32_t>(height), app.device->NativeFormat())
    );
    Fill(*canvas, layout, app, keymap, state, {});

    OverlayState overlay;
    overlay.name = kKeyboardName;
    overlay.grabbable = true;
    overlay.recenter = true;
    overlay.interactable = true;
    overlay.spawn_scale = width / kPixelsPerUnit * kMetersPerUnit * app.config.keyboard_scale;
    overlay.spawn_point = Eigen::Vector3f(0.0f, -kKeyboardDrop, -kKeyboardReach);
    overlay.spawn_rotation = Eigen::Quaternionf(
        Eigen::AngleAxisf(-std::atan2(kKeyboardDrop, kKeyboardReach), Eigen::Vector3f::UnitX())
    );
    overlay.interaction_transform = Eigen::Translation2f(0.5f, 0.5f) * Eigen::Scaling(1.0f, -width / height);

    return std::make_unique<Overlay>(std::move(overlay), std::make_unique<KeyboardBackend>(std::move(canvas), std::move(state)));
}

auto Keyboard::SwitchProfile(AppState& app, std::shared_ptr<const Keymap> keymap, std::string profile) -> void {
    LOG_INFO("Switching keyboard profile to {}", profile);
    app.config.keyboard_profile = std::move(profile);
    app.tasks.Enqueue(
        ModifyOverlayTask{
            .selector = std::string(kKeyboardName),
            .modify = [keymap = std::move(keymap)](AppState& state, OverlayState& previous) -> void {
                state.tasks.Enqueue(DropOverlayTask{.selector = std::string(kKeyboardName)});
                state.tasks.Enqueue(
                    CreateOverlayTask{
                        .selector = std::string(kKeyboardName),
                        .create = [keymap, previous](AppState& app_state)
                            -> std::optional<std::pair<OverlayState, std::unique_ptr<OverlayBackend>>> {
                            auto created = Keyboard::Create(app_state, keymap);
                            if (!created) {
                                LOG_ERROR("Could not create keyboard: {}", created.error().Message());
                                return std::nullopt;
                            }
                            auto& overlay = **created;
                            auto& next = overlay.state;
                            const float factor = previous.spawn_scale > 0.0f ? next.spawn_scale / previous.spawn_scale : 1.0f;
                            const auto rescale = [factor](Eigen::Affine3f transform) -> Eigen::Affine3f {
                                transform.linear() *= factor;
                                return transform;
                            };
                            next.want_visible = previous.want_visible;
                            next.show_hide = previous.show_hide;
                            next.recenter = previous.recenter;
                            next.grabbable = previous.grabbable;
                            next.interactable = previous.interactable;
                            next.alpha = previous.alpha;
                            next.curvature = previous.curvature;
                            next.transform = rescale(previous.transform);
                            if (previous.saved_transform) {
                                next.saved_transform = rescale(*previous.saved_transform);
                                for (auto& [name, saved] : app_state.config.transform_values) {
                                    if (name == kKeyboardName) {
                                        saved = *next.saved_transform;
                                    }
                                }
                            }
                            next.dirty = true;
                            return std::pair{std::move(next), std::move(overlay.backend)};
                        },
                    }
                );
            },
        }
    );
}
