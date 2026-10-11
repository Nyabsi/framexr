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
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Button.hpp"
#include "Canvas.hpp"
#include "Control.hpp"
#include "Error.hpp"
#include "KeyTranslator.hpp"
#include "KeyboardLayout.hpp"
#include "Keymap.hpp"
#include "Overlay.hpp"
#include "VirtualKey.hpp"

class AppState;

constexpr std::string_view kKeyboardName = "kbd";

struct LetterLabel {
    Control* label = nullptr;
    std::string lower;
    std::string upper;
};

struct KeyboardState {
    uint8_t modifiers = 0;
    uint8_t alt_modifier = 0;
    bool caps_lock = false;
    std::shared_ptr<const KeyTranslator> translator;
    std::shared_ptr<const Keymap> typing_keymap;
    std::vector<LetterLabel> letters;

    auto RefreshLetterCase() -> void;
    [[nodiscard]] auto Text(VirtualKey key, uint8_t pressed_modifiers) const -> std::string;
};

class KeyControl final : public Control {
  public:
    KeyControl(std::shared_ptr<KeyboardState> state, VirtualKey key);

    auto OnPress(AppState& app, PointerMode mode) -> void override;
    auto OnRelease(AppState& app) -> void override;
    auto HighlightColor(AppState& app) -> std::optional<Eigen::Vector4f> override;
    auto Outline(AppState& app) -> std::optional<HighlightOutline> override;

    [[nodiscard]] auto Latched() const -> bool { return latched_; }

  private:
    std::shared_ptr<KeyboardState> state_;
    VirtualKey key_;
    VirtualKey sent_;
    bool latches_;
    bool latched_ = false;
    bool translated_ = false;
    bool captured_ = false;
};

class ModifierControl final : public Control {
  public:
    ModifierControl(std::shared_ptr<KeyboardState> state, uint8_t modifier);

    auto OnPress(AppState& app, PointerMode mode) -> void override;
    auto OnRelease(AppState& app) -> void override;
    auto HighlightColor(AppState& app) -> std::optional<Eigen::Vector4f> override;

  private:
    std::shared_ptr<KeyboardState> state_;
    uint8_t modifier_;
    bool sticky_ = false;
    bool captured_ = false;
};

class ActionKeyControl final : public Control {
  public:
    explicit ActionKeyControl(ActionKey action);

    auto OnPress(AppState& app, PointerMode mode) -> void override;
    auto OnRelease(AppState& app) -> void override;

  private:
    ActionKey action_;
    ActionRunner press_runner_;
    ActionRunner release_runner_;
};

class ProfileControl final : public Control {
  public:
    ProfileControl(std::shared_ptr<const Keymap> keymap, std::string current);

    auto OnPress(AppState& app, PointerMode mode) -> void override;

  private:
    std::shared_ptr<const Keymap> keymap_;
    std::string current_;
};

class KeyboardBackend final : public OverlayBackend {
  public:
    KeyboardBackend(std::unique_ptr<Canvas> canvas, std::shared_ptr<KeyboardState> state);

    auto SetRenderer(std::unique_ptr<OverlayRenderer> renderer) -> void override;
    auto SetInteraction(std::unique_ptr<InteractionHandler> interaction) -> void override;

    auto Init(AppState& app) -> Result<> override;
    auto Pause(AppState& app) -> Result<> override;
    auto Resume(AppState& app) -> Result<> override;
    auto ShouldRender(AppState& app) -> Result<RenderReadiness> override;
    auto Render(AppState& app, const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> override;
    auto Meta() -> std::optional<FrameMeta> override;

    auto OnHover(AppState& app, const PointerHit& hit) -> std::optional<Haptics> override;
    auto OnLeft(AppState& app, size_t pointer) -> void override;
    auto OnPointer(AppState& app, const PointerHit& hit, bool pressed) -> void override;
    auto OnScroll(AppState& app, const PointerHit& hit, float delta_y, float delta_x) -> void override;

  private:
    std::unique_ptr<Canvas> canvas_;
    std::shared_ptr<KeyboardState> state_;
};

struct KeyboardPlacement {
    float scale = 1.0f;
    Eigen::Vector2f origin = Eigen::Vector2f::Zero();
    bool interactive = true;
};

class Keyboard final {
  public:
    static constexpr float kPixelsPerUnit = 80.0f;
    static constexpr float kKeyboardMargin = 14.0f;

    static auto Create(AppState& app, const std::shared_ptr<const Keymap>& keymap) -> Result<std::unique_ptr<Overlay>>;
    static auto CreateFrom(AppState& app, const std::shared_ptr<const Keymap>& keymap, const KeyboardLayout& layout)
        -> Result<std::unique_ptr<Overlay>>;
    static auto CanvasSize(const KeyboardLayout& layout) -> Eigen::Vector2f;
    static auto KeyRect(const KeyDefinition& key, const KeyboardPlacement& placement) -> CanvasRect;
    static auto Fill(
        Canvas& canvas,
        const KeyboardLayout& layout,
        AppState& app,
        const std::shared_ptr<const Keymap>& keymap,
        const std::shared_ptr<KeyboardState>& state,
        const KeyboardPlacement& placement
    ) -> void;
    static auto SwitchProfile(AppState& app, std::shared_ptr<const Keymap> keymap, std::string profile) -> void;
    static auto IsLatchingKey(VirtualKey key) -> bool;
    static auto SendKey(AppState& app, VirtualKey key, bool down) -> void;
    static auto SetModifiers(AppState& app, uint8_t modifiers) -> void;
    static auto Click(AppState& app) -> void;
};
