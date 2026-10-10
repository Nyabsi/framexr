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
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "Canvas.hpp"
#include "Control.hpp"
#include "Error.hpp"
#include "Keymap.hpp"
#include "Overlay.hpp"
#include "Process.hpp"
#include "VirtualKey.hpp"

class AppState;

constexpr std::string_view kKeyboardName = "kbd";
constexpr std::string_view kProfileSwitchKey = "PROFILE";

enum class AltModifier : uint8_t {
    None,
    Shift,
    Ctrl,
    Alt,
    Super,
    Meta,
};

struct KeyboardLayout {
    std::string name;
    float row_size = 0.0f;
    std::vector<std::vector<float>> key_sizes;
    std::vector<std::vector<std::optional<std::string>>> main_layout;
    AltModifier alt_modifier = AltModifier::None;
    std::map<std::string, std::vector<std::string>, std::less<>> exec_commands;
    std::map<std::string, std::vector<std::string>, std::less<>> macros;
    std::map<std::string, std::vector<std::string>, std::less<>> labels;
    std::optional<bool> auto_labels;

    static auto Parse(std::string_view text) -> Result<KeyboardLayout>;

    [[nodiscard]] auto Validate() const -> Result<>;
    [[nodiscard]] auto LabelForKey(std::string_view key) const -> std::vector<std::string>;
    [[nodiscard]] auto AltModifierMask() const -> uint8_t;
};

class KeyboardProfiles final {
  public:
    static auto All() -> const std::vector<KeyboardLayout>&;
    static auto Find(std::string_view name) -> const KeyboardLayout&;
    static auto After(std::string_view name) -> const KeyboardLayout&;
    static auto Load(std::span<const std::string_view> built_in, const std::filesystem::path& user_directory)
        -> std::vector<KeyboardLayout>;
};

auto from_json(const nlohmann::json& json, AltModifier& value) -> void;
auto from_json(const nlohmann::json& json, KeyboardLayout& value) -> void;

struct LetterLabel {
    Control* label = nullptr;
    std::string lower;
    std::string upper;
};

struct KeyboardState {
    uint8_t modifiers = 0;
    uint8_t alt_modifier = 0;
    bool caps_lock = false;
    std::vector<Process> processes;
    std::vector<LetterLabel> letters;

    auto ReapProcesses() -> void;
    auto RefreshLetterCase() -> void;
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
    bool latches_;
    bool latched_ = false;
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
};

class MacroControl final : public Control {
  public:
    explicit MacroControl(std::vector<std::pair<VirtualKey, bool>> events);

    auto OnPress(AppState& app, PointerMode mode) -> void override;

  private:
    std::vector<std::pair<VirtualKey, bool>> events_;
};

class ExecControl final : public Control {
  public:
    ExecControl(std::shared_ptr<KeyboardState> state, std::vector<std::string> press, std::vector<std::string> release);

    auto OnPress(AppState& app, PointerMode mode) -> void override;
    auto OnRelease(AppState& app) -> void override;

  private:
    auto Spawn(const std::vector<std::string>& command) -> void;

    std::shared_ptr<KeyboardState> state_;
    std::vector<std::string> press_;
    std::vector<std::string> release_;
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

class Keyboard final {
  public:
    static auto Create(AppState& app, const std::shared_ptr<const Keymap>& keymap) -> Result<std::unique_ptr<Overlay>>;
    static auto SwitchProfile(AppState& app, std::shared_ptr<const Keymap> keymap, std::string profile) -> void;
    static auto IsLatchingKey(VirtualKey key) -> bool;
    static auto SendKey(AppState& app, VirtualKey key, bool down) -> void;
    static auto SetModifiers(AppState& app, uint8_t modifiers) -> void;
    static auto Click(AppState& app) -> void;
    static auto MacroEvents(const std::vector<std::string>& verbs) -> std::vector<std::pair<VirtualKey, bool>>;
};
