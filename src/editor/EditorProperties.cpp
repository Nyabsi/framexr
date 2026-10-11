// FrameXR
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
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

#include <algorithm>
#include <array>
#include <cstddef>
#include <numbers>
#include <chrono>
#include <stdexcept>
#include <cctype>
#include <cctype>
#include <cmath>
#include <format>
#include <ranges>
#include <span>
#include <utility>

#include "CommandBuffer.hpp"
#include "Config.hpp"
#include "Device.hpp"
#include "Editor.hpp"
#include "Keyboard.hpp"
#include "State.hpp"
#include "Theme.hpp"
#include "Watch.hpp"

namespace {

constexpr size_t kRecentColors = 8;
constexpr size_t kSummaryLength = 26;

struct KeyCategory {
    std::string_view name;
    std::span<const VirtualKey> keys;
};

constexpr auto kLetters = std::to_array<VirtualKey>({
    VirtualKey::A, VirtualKey::B, VirtualKey::C, VirtualKey::D, VirtualKey::E, VirtualKey::F, VirtualKey::G,
    VirtualKey::H, VirtualKey::I, VirtualKey::J, VirtualKey::K, VirtualKey::L, VirtualKey::M, VirtualKey::N,
    VirtualKey::O, VirtualKey::P, VirtualKey::Q, VirtualKey::R, VirtualKey::S, VirtualKey::T, VirtualKey::U,
    VirtualKey::V, VirtualKey::W, VirtualKey::X, VirtualKey::Y, VirtualKey::Z,
});
constexpr auto kDigits = std::to_array<VirtualKey>({
    VirtualKey::N1,
    VirtualKey::N2,
    VirtualKey::N3,
    VirtualKey::N4,
    VirtualKey::N5,
    VirtualKey::N6,
    VirtualKey::N7,
    VirtualKey::N8,
    VirtualKey::N9,
    VirtualKey::N0,
});
constexpr auto kSymbols = std::to_array<VirtualKey>({
    VirtualKey::Oem3,
    VirtualKey::Minus,
    VirtualKey::Plus,
    VirtualKey::Oem4,
    VirtualKey::Oem6,
    VirtualKey::Oem5,
    VirtualKey::Oem1,
    VirtualKey::Oem7,
    VirtualKey::Comma,
    VirtualKey::Period,
    VirtualKey::Oem2,
    VirtualKey::Oem102,
});
constexpr auto kFunctionKeys = std::to_array<VirtualKey>({
    VirtualKey::F1,  VirtualKey::F2,  VirtualKey::F3,  VirtualKey::F4,  VirtualKey::F5,  VirtualKey::F6,
    VirtualKey::F7,  VirtualKey::F8,  VirtualKey::F9,  VirtualKey::F10, VirtualKey::F11, VirtualKey::F12,
    VirtualKey::F13, VirtualKey::F14, VirtualKey::F15, VirtualKey::F16, VirtualKey::F17, VirtualKey::F18,
    VirtualKey::F19, VirtualKey::F20, VirtualKey::F21, VirtualKey::F22, VirtualKey::F23, VirtualKey::F24,
});
constexpr auto kEditingKeys = std::to_array<VirtualKey>({
    VirtualKey::Escape,
    VirtualKey::Tab,
    VirtualKey::Return,
    VirtualKey::BackSpace,
    VirtualKey::Space,
    VirtualKey::Delete,
    VirtualKey::Insert,
    VirtualKey::Caps,
});
constexpr auto kNavigationKeys = std::to_array<VirtualKey>({
    VirtualKey::Home,
    VirtualKey::End,
    VirtualKey::Prior,
    VirtualKey::Next,
    VirtualKey::Up,
    VirtualKey::Down,
    VirtualKey::Left,
    VirtualKey::Right,
    VirtualKey::Print,
    VirtualKey::Scroll,
    VirtualKey::Pause,
    VirtualKey::Menu,
});
constexpr auto kModifierKeys = std::to_array<VirtualKey>({
    VirtualKey::LShift,
    VirtualKey::RShift,
    VirtualKey::LCtrl,
    VirtualKey::RCtrl,
    VirtualKey::LAlt,
    VirtualKey::Meta,
    VirtualKey::LSuper,
    VirtualKey::RSuper,
    VirtualKey::NumLock,
});
constexpr auto kNumpadKeys = std::to_array<VirtualKey>({
    VirtualKey::Kp0,
    VirtualKey::Kp1,
    VirtualKey::Kp2,
    VirtualKey::Kp3,
    VirtualKey::Kp4,
    VirtualKey::Kp5,
    VirtualKey::Kp6,
    VirtualKey::Kp7,
    VirtualKey::Kp8,
    VirtualKey::Kp9,
    VirtualKey::KpDecimal,
    VirtualKey::KpEnter,
    VirtualKey::KpAdd,
    VirtualKey::KpSubtract,
    VirtualKey::KpMultiply,
    VirtualKey::KpDivide,
});
constexpr auto kMediaKeys = std::to_array<VirtualKey>({
    VirtualKey::Xf86AudioMute,
    VirtualKey::Xf86AudioLowerVolume,
    VirtualKey::Xf86AudioRaiseVolume,
    VirtualKey::Xf86AudioPlay,
    VirtualKey::Xf86AudioPrev,
    VirtualKey::Xf86AudioNext,
    VirtualKey::Xf86AudioStop,
});

constexpr auto kKeyCategories = std::to_array<KeyCategory>({
    {"Letters", kLetters},
    {"Digits", kDigits},
    {"Symbols", kSymbols},
    {"F-keys", kFunctionKeys},
    {"Editing", kEditingKeys},
    {"Moving", kNavigationKeys},
    {"Modifiers", kModifierKeys},
    {"Numpad", kNumpadKeys},
    {"Media", kMediaKeys},
});

constexpr auto kAltModifiers = std::to_array<std::pair<AltModifier, std::string_view>>({
    {AltModifier::None, "None"},
    {AltModifier::Shift, "Shift"},
    {AltModifier::Ctrl, "Ctrl"},
    {AltModifier::Alt, "Alt"},
    {AltModifier::Super, "Super"},
    {AltModifier::Meta, "AltGr"},
});

constexpr auto kRoundings = std::to_array<Rounding>({Rounding::Square, Rounding::Subtle, Rounding::Round});

constexpr size_t kZoneResults = 16;

auto AllZones() -> const std::vector<std::string>& {
    static const auto zones = []() -> std::vector<std::string> {
        std::vector<std::string> names;
        try {
            for (const auto& zone : std::chrono::get_tzdb().zones) {
                names.emplace_back(zone.name());
            }
        } catch (const std::runtime_error&) {
            return {};
        }
        std::ranges::sort(names);
        return names;
    }();
    return zones;
}

auto Searchable(std::string_view text) -> std::string {
    std::string folded(text);
    std::ranges::transform(folded, folded.begin(), [](unsigned char c) -> char {
        return c == '_' ? ' ' : static_cast<char>(std::tolower(c));
    });
    return folded;
}

auto ZoneName(const std::variant<size_t, std::string>& zone, const AppState& app) -> std::string {
    std::string name;
    if (const auto* index = std::get_if<size_t>(&zone)) {
        name = *index < app.config.timezones.size() ? app.config.timezones[*index] : std::string("Local");
    } else {
        name = std::get<std::string>(zone);
    }
    std::ranges::replace(name, '_', ' ');
    return name;
}

auto ZoneName(const std::optional<std::variant<size_t, std::string>>& zone, const AppState& app) -> std::string {
    return zone ? ZoneName(*zone, app) : std::string("Local");
}

auto LocalZone() -> std::string {
    try {
        return std::string(std::chrono::current_zone()->name());
    } catch (const std::runtime_error&) {
        return "UTC";
    }
}

auto Shorten(std::string text) -> std::string {
    if (text.size() > kSummaryLength) {
        text.resize(kSummaryLength - 3);
        text += "...";
    }
    return text;
}

auto ElementFont(const ModularElement& element) -> std::optional<int> {
    return std::visit(
        [](const auto& item) -> std::optional<int> {
            if constexpr (requires { item.font_size; }) {
                return item.font_size;
            } else {
                return std::nullopt;
            }
        },
        element
    );
}

auto SetElementFont(ModularElement& element, int font_size) -> void {
    std::visit(
        [font_size](auto& item) -> void {
            if constexpr (requires { item.font_size; }) {
                item.font_size = std::clamp(font_size, 6, 200);
            }
        },
        element
    );
}

auto ElementColor(const ModularElement& element, bool background) -> std::optional<std::string> {
    return std::visit(
        [background](const auto& item) -> std::optional<std::string> {
            if constexpr (requires { item.bg_color; }) {
                if (background) {
                    return item.bg_color;
                }
            }
            if constexpr (requires { item.fg_color; }) {
                if (!background) {
                    return item.fg_color;
                }
            }
            return std::nullopt;
        },
        element
    );
}

auto SetElementColor(ModularElement& element, bool background, const std::string& color) -> void {
    std::visit(
        [background, &color](auto& item) -> void {
            if constexpr (requires { item.bg_color; }) {
                if (background) {
                    item.bg_color = color;
                }
            }
            if constexpr (requires { item.fg_color; }) {
                if (!background) {
                    item.fg_color = color;
                }
            }
        },
        element
    );
}

auto ElementRadius(const ModularElement& element) -> std::optional<std::optional<float>> {
    return std::visit(
        [](const auto& item) -> std::optional<std::optional<float>> {
            if constexpr (requires { item.corner_radius; }) {
                return item.corner_radius;
            } else {
                return std::nullopt;
            }
        },
        element
    );
}

auto SetElementRadius(ModularElement& element, std::optional<float> radius) -> void {
    std::visit(
        [radius](auto& item) -> void {
            if constexpr (requires { item.corner_radius; }) {
                item.corner_radius = radius;
            }
        },
        element
    );
}

auto PrettyType(const ModularElement& element) -> std::string_view {
    return std::visit(
        [](const auto& item) -> std::string_view {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, PanelElement>) {
                return "Panel";
            } else if constexpr (std::is_same_v<Item, LabelElement>) {
                return std::holds_alternative<ClockLabelContent>(item.content) ? "Clock" : "Text";
            } else if constexpr (std::is_same_v<Item, SpriteElement>) {
                return "Picture";
            } else if constexpr (std::is_same_v<Item, ButtonElement>) {
                return "Button";
            } else if constexpr (std::is_same_v<Item, BatteryListElement>) {
                return "Batteries";
            } else {
                return "Screens";
            }
        },
        element
    );
}

auto TriggerActions(ButtonData& data, ButtonTrigger trigger) -> std::optional<std::vector<ButtonAction>>& {
    switch (trigger) {
    case ButtonTrigger::LeftClick: return data.click_up;
    case ButtonTrigger::RightClick: return data.right_up;
    case ButtonTrigger::ScrollUp: return data.scroll_up;
    case ButtonTrigger::ScrollDown: return data.scroll_down;
    }
    return data.click_up;
}

auto TriggerName(ButtonTrigger trigger) -> std::string_view {
    switch (trigger) {
    case ButtonTrigger::LeftClick: return "click";
    case ButtonTrigger::RightClick: return "right click";
    case ButtonTrigger::ScrollUp: return "scroll up";
    case ButtonTrigger::ScrollDown: return "scroll down";
    }
    return {};
}

auto HsvRgb(float hue, float saturation, float value) -> std::array<float, 3> {
    const float c = value * saturation;
    const float h = std::fmod(std::fmod(hue, 1.0f) + 1.0f, 1.0f) * 6.0f;
    const float x = c * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));
    std::array<float, 3> rgb{};
    if (h < 1.0f) {
        rgb = {c, x, 0.0f};
    } else if (h < 2.0f) {
        rgb = {x, c, 0.0f};
    } else if (h < 3.0f) {
        rgb = {0.0f, c, x};
    } else if (h < 4.0f) {
        rgb = {0.0f, x, c};
    } else if (h < 5.0f) {
        rgb = {x, 0.0f, c};
    } else {
        rgb = {c, 0.0f, x};
    }
    const float m = value - c;
    return {rgb[0] + m, rgb[1] + m, rgb[2] + m};
}

auto HsvHex(float hue, float saturation, float value) -> std::string {
    const auto rgb = HsvRgb(hue, saturation, value);
    const auto byte = [](float channel) -> int { return static_cast<int>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f)); };
    return std::format("#{:02x}{:02x}{:02x}", byte(rgb[0]), byte(rgb[1]), byte(rgb[2]));
}

auto HexHsv(const std::string& hex) -> std::array<float, 3> {
    const auto color = EditorColors::Hex(hex);
    const float r = color.x();
    const float g = color.y();
    const float b = color.z();
    const float high = std::max({r, g, b});
    const float delta = high - std::min({r, g, b});
    float hue = 0.0f;
    if (delta > 0.0f) {
        if (high == r) {
            hue = std::fmod((g - b) / delta, 6.0f);
        } else if (high == g) {
            hue = (b - r) / delta + 2.0f;
        } else {
            hue = (r - g) / delta + 4.0f;
        }
        hue /= 6.0f;
        if (hue < 0.0f) {
            hue += 1.0f;
        }
    }
    return {hue, high > 0.0f ? delta / high : 0.0f, high};
}

auto UploadPixels(AppState& app, uint32_t width, uint32_t height, const std::vector<std::byte>& pixels) -> std::shared_ptr<Image> {
    auto command_buffer = app.device->CreateCommandBuffer(app.device->TransferQueue());
    if (!command_buffer) {
        return nullptr;
    }
    auto image = command_buffer->UploadImage(width, height, VK_FORMAT_R8G8B8A8_UNORM, pixels);
    if (!image || !command_buffer->SubmitUpload()) {
        return nullptr;
    }
    return std::move(*image);
}

auto ToByte(float value) -> std::byte {
    return static_cast<std::byte>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

auto KeyCaption(VirtualKey key) -> std::string {
    const KeyboardLayout empty;
    const auto labels = empty.Labels({.action = TypeKey{.key = key}});
    std::string caption;
    for (const auto& label : labels) {
        if (!caption.empty()) {
            caption += ' ';
        }
        caption += label;
    }
    return caption.empty() ? std::string(VirtualKeys::Name(key)) : caption;
}

auto ShortcutText(const std::vector<VirtualKey>& keys) -> std::string {
    std::string text;
    for (const auto key : keys) {
        if (!text.empty()) {
            text += " + ";
        }
        text += VirtualKeys::Name(key);
    }
    return text.empty() ? std::string("(none)") : text;
}

auto CommandOf(const std::vector<ButtonAction>& actions) -> std::optional<std::vector<std::string>> {
    for (const auto& action : actions) {
        if (const auto* exec = std::get_if<ExecButtonAction>(&action)) {
            return exec->command;
        }
    }
    return std::nullopt;
}

auto ShortcutOf(const std::vector<ButtonAction>& actions) -> const ShortcutButtonAction* {
    for (const auto& action : actions) {
        if (const auto* shortcut = std::get_if<ShortcutButtonAction>(&action)) {
            return shortcut;
        }
    }
    return nullptr;
}

auto CommandActions(const std::string& text) -> std::vector<ButtonAction> {
    auto command = Editor::SplitCommand(text);
    if (command.empty()) {
        return {};
    }
    return {ExecButtonAction{.command = std::move(command)}};
}

} // namespace

auto Editor::Describe(const ButtonAction& action) -> std::string {
    return std::visit(
        [](const auto& item) -> std::string {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, ExecButtonAction>) {
                return Shorten(std::format("Run {}", JoinCommand(item.command)));
            } else if constexpr (std::is_same_v<Item, VirtualKeyButtonAction>) {
                return std::format("Key {}", VirtualKeys::Name(item.keycode));
            } else if constexpr (std::is_same_v<Item, WatchButtonAction>) {
                return "Watch setting";
            } else if constexpr (std::is_same_v<Item, OverlayButtonAction>) {
                if (const auto* name = std::get_if<std::string>(&item.target)) {
                    return Shorten(std::format("Toggle {}", *name == kKeyboardName ? "keyboard" : *name));
                }
                return "Toggle screen";
            } else if constexpr (std::is_same_v<Item, WindowButtonAction>) {
                return Shorten(std::format("{} {}", item.action == WindowAction::Destroy ? "Close" : "Open", item.target));
            } else if constexpr (std::is_same_v<Item, ToastButtonAction>) {
                return Shorten(std::format("Notify {}", item.message));
            } else if constexpr (std::is_same_v<Item, ColorAdjustButtonAction>) {
                return "Colour gain";
            } else if constexpr (std::is_same_v<Item, DragMultiplierButtonAction>) {
                return "Drag speed";
            } else if constexpr (std::is_same_v<Item, SystemButtonAction>) {
                switch (item.action) {
                case SystemAction::Recenter: return "Recenter";
                case SystemAction::ShowHide: return "Show/hide";
                case SystemAction::Edit: return "Open editor";
                case SystemAction::ToggleAllowSliding:
                case SystemAction::ToggleNotificationSounds:
                case SystemAction::ToggleNotifications:
                case SystemAction::ToggleRotateLock:
                case SystemAction::PlayspaceResetOffset:
                case SystemAction::PlayspaceFixFloor:
                case SystemAction::RecalculateExtent:
                case SystemAction::PersistConfig:
                case SystemAction::PersistLayout: break;
                }
                return "System";
            } else if constexpr (std::is_same_v<Item, SendOscButtonAction>) {
                return Shorten(std::format("OSC {}", item.parameter));
            } else if constexpr (std::is_same_v<Item, ShortcutButtonAction>) {
                return Shorten(ShortcutText(item.keys));
            } else if constexpr (std::is_same_v<Item, TypeTextButtonAction>) {
                return Shorten(std::format("Type {}", item.text));
            } else {
                return "Next layout";
            }
        },
        action
    );
}

auto Editor::TextRow(
    PanelBuilder& builder,
    AppState&,
    std::string_view label,
    const std::string& id,
    const std::string& value,
    const std::function<void(Editor&, AppState&, const std::string&)>& commit
) -> void {
    const bool focused = focus_ && focus_->id == id;
    const std::string shown = focused ? focus_->buffer : value;
    builder.Field(label, shown, focused, [this, id, value, commit](AppState& state) -> void {
        BeginText(state, id, value, commit);
    });
}

auto Editor::ColorRow(
    PanelBuilder& builder,
    std::string_view label,
    const std::string& title,
    const std::function<std::string(const Editor&)>& get,
    const std::function<void(Editor&, const std::string&)>& set,
    const std::function<void(Editor&)>& reset
) -> void {
    const auto current = get(*this);
    builder.Swatch(label, current, [this, title, get, set, reset, current](AppState&) -> void {
        const auto [hue, saturation, value] = HexHsv(current);
        OpenColor({.title = title, .get = get, .set = set, .reset = reset, .hue = hue, .saturation = saturation, .value = value});
    });
}

auto Editor::BuildToolbar(PanelBuilder& builder, AppState&) -> void {
    if (closing_prompt_) {
        builder.Row({
            {.text = "Unsaved changes", .weight = 1.6f, .text_color = EditorColors::Dim()},
            {.text = "Save all & close",
             .click = [this](AppState& app) -> void {
                 const auto previous = target;
                 for (const auto edit : {EditTarget::Watch, EditTarget::Keyboard, EditTarget::Theme}) {
                     target = edit;
                     const bool changed = edit == EditTarget::Watch    ? watch.history.Dirty()
                                        : edit == EditTarget::Keyboard ? keyboard.history.Dirty()
                                                                       : theme.Dirty();
                     if (changed) {
                         Save(app);
                     }
                 }
                 target = previous;
                 Close(app);
             }},
            {.text = "Keep draft", .click = [this](AppState& app) -> void { Suspend(app); }},
            {.text = "Discard",
             .click = [this](AppState& app) -> void { Discard(app); },
             .color = EditorColors::Danger(),
             .text_color = EditorColors::OnAccent()},
            {.text = "Keep editing", .click = [this](AppState&) -> void {
                 closing_prompt_ = false;
                 Touch();
             }},
        });
        return;
    }
    const auto dim = [](bool enabled) -> std::optional<Eigen::Vector4f> {
        return enabled ? std::nullopt : std::optional(EditorColors::Dark());
    };
    bool can_undo = theme.CanUndo();
    bool can_redo = theme.CanRedo();
    bool dirty = theme.Dirty();
    if (target == EditTarget::Watch) {
        can_undo = watch.history.CanUndo();
        can_redo = watch.history.CanRedo();
        dirty = watch.history.Dirty();
    } else if (target == EditTarget::Keyboard) {
        can_undo = keyboard.history.CanUndo();
        can_redo = keyboard.history.CanRedo();
        dirty = keyboard.history.Dirty();
    }
    std::vector<Cell> cells{
        {.text = "Watch",
         .click = [this](AppState&) -> void { SwitchTarget(EditTarget::Watch); },
         .selected = target == EditTarget::Watch},
        {.text = "Keyboard",
         .click = [this](AppState&) -> void { SwitchTarget(EditTarget::Keyboard); },
         .weight = 1.2f,
         .selected = target == EditTarget::Keyboard},
        {.text = "Theme",
         .click = [this](AppState&) -> void { SwitchTarget(EditTarget::Theme); },
         .selected = target == EditTarget::Theme},

        {.text = "Undo", .click = [this](AppState& app) -> void { Undo(app); }, .color = dim(can_undo)},
        {.text = "Redo", .click = [this](AppState& app) -> void { Redo(app); }, .color = dim(can_redo)},
    };
    if (target == EditTarget::Watch) {
        cells.push_back(
            {.text = "Snap",
             .click = [this](AppState&) -> void {
                 watch.snap = !watch.snap;
                 Touch();
             },
             .selected = watch.snap}
        );
        cells.push_back({.text = "Layers", .click = [this](AppState&) -> void { SetExploded(!exploded_); }, .selected = exploded_});
    }
    cells.push_back(
        {.text = "On hand",
         .click = [this](AppState&) -> void {
             palette_on_hand_ = !palette_on_hand_;
             Touch();
         },
         .weight = 1.2f,
         .selected = palette_on_hand_}
    );
    cells.push_back({.text = dirty ? "Save*" : "Save", .click = [this](AppState& app) -> void { Save(app); }, .selected = dirty});
    cells.push_back({
        .text = revert_armed_ ? "Sure?" : "Revert",
        .click = [this](AppState& app) -> void { Revert(app); },
        .color = revert_armed_ ? std::optional(EditorColors::Danger()) : std::nullopt,
        .text_color = revert_armed_ ? std::optional(EditorColors::OnAccent()) : std::nullopt,
    });
    cells.push_back({.text = "Done", .click = [this](AppState& app) -> void { Done(app); }});
    for (auto& cell : cells) {
        cell.weight = 0.0f;
    }
    builder.Row(std::move(cells));
}

auto Editor::BuildPalette(PanelBuilder& builder, AppState&) -> void {
    if (target == EditTarget::Theme) {
        builder.Title("Theme");
        builder.Caption("Pick a colour on the right");
        builder.Caption("to change it everywhere");
        return;
    }
    builder.Title("Add");
    builder.Caption("Drag onto the preview");
    if (target == EditTarget::Watch) {
        for (const auto part : FormDesigner::kParts) {
            builder.Button({.text = std::string(FormDesigner::PartName(part)), .press = [this, part](AppState& app) -> void {
                                StartCarry(app, part);
                            }});
        }
        return;
    }
    for (const auto key_template : KeyboardDesigner::kTemplates) {
        builder.Button(
            {.text = std::string(KeyboardDesigner::TemplateName(key_template)),
             .press = [this, key_template](AppState& app) -> void { StartCarry(app, key_template); }}
        );
    }
}

auto Editor::BuildProperties(PanelBuilder& builder, AppState& app) -> void {
    if (capture_) {
        builder.Title(capture_->shortcut ? "Record shortcut" : "Press a key");
        builder.Caption(capture_->shortcut ? "Tap modifiers, then the key" : "Press it on the keyboard");
        if (capture_->shortcut) {
            builder.Caption(ShortcutText(capture_->held));
        }
        builder.Button({.text = "Cancel", .click = [this](AppState& state) -> void { EndText(state); }});
        return;
    }
    if (target == EditTarget::Theme) {
        BuildThemeProperties(builder, app);
        return;
    }
    if (target == EditTarget::Watch) {
        if (const auto selected = watch.selected; selected && *selected < watch.Document().elements.size()) {
            BuildWatchProperties(builder, app, *selected);
        } else if (const auto form = watch.selected_form; form && *form > 0 && *form < watch.Document().FormCount()) {
            BuildExtraFormProperties(builder, *form);
        } else {
            BuildFormProperties(builder, app);
        }
        return;
    }
    if (const auto selected = keyboard.selected; selected && *selected < keyboard.Document().keys.size()) {
        BuildKeyProperties(builder, app, *selected);
    } else {
        BuildKeyboardProperties(builder, app);
    }
}

auto Editor::BuildFormProperties(PanelBuilder& builder, AppState& app) -> void {
    const auto& config = watch.Document();
    builder.Title("Watch");
    builder.Section("Watch layouts");
    {
        const auto profiles = Watch::Profiles();
        for (size_t start = 0; start < profiles.size(); start += 2) {
            std::vector<Cell> cells;
            for (size_t i = start; i < std::min(start + 2, profiles.size()); ++i) {
                const auto& name = profiles[i];
                cells.push_back({
                    .text = name,
                    .click = [this, name](AppState& state) -> void { SelectWatch(state, name); },
                    .selected = name == watch_profile_,
                });
            }
            if (cells.size() < 2) {
                cells.push_back({.text = ""});
            }
            builder.Row(std::move(cells), 30.0f);
        }
        std::vector<Cell> actions{{.text = "New layout", .click = [this](AppState& state) -> void { NewWatch(state); }}};
        if (!Watch::IsBuiltIn(watch_profile_)) {
            actions.push_back({
                .text = delete_armed_ ? "Sure?" : "Delete",
                .click = [this](AppState& state) -> void { DeleteWatch(state); },
                .color = EditorColors::Danger(),
                .text_color = EditorColors::OnAccent(),
            });
        }
        builder.Row(std::move(actions));
        if (!Watch::IsBuiltIn(watch_profile_)) {
            TextRow(
                builder,
                app,
                "Name",
                "watch.name",
                watch_profile_,
                [](Editor& editor, AppState& state, const std::string& text) -> void { editor.RenameWatch(state, text); }
            );
        }
    }
    builder.Section("Size");
    builder.Stepper(
        "Width",
        std::format("{:.0f}", config.form.width),
        [this](AppState&) -> void {
            EditWatch([this](ModularUiConfig& edited) -> void {
                edited.form.width = std::max(edited.form.width - 10.0f, watch.MinimumForm(edited).x());
            });
        },
        [this](AppState&) -> void { EditWatch([](ModularUiConfig& edited) -> void { edited.form.width += 10.0f; }); },
        {}
    );
    builder.Stepper(
        "Height",
        std::format("{:.0f}", config.form.height),
        [this](AppState&) -> void {
            EditWatch([this](ModularUiConfig& edited) -> void {
                edited.form.height = std::max(edited.form.height - 10.0f, watch.MinimumForm(edited).y());
            });
        },
        [this](AppState&) -> void { EditWatch([](ModularUiConfig& edited) -> void { edited.form.height += 10.0f; }); },
        {}
    );
    builder.Stepper(
        "On the wrist",
        std::format("{:.0f} mm", config.width * 1000.0f),
        [this](AppState&) -> void {
            EditWatch([](ModularUiConfig& edited) -> void { edited.width = std::max(edited.width - 0.005f, 0.03f); });
        },
        [this](AppState&) -> void { EditWatch([](ModularUiConfig& edited) -> void { edited.width += 0.005f; }); },
        {}
    );
    builder.Gap();
    if (config.form.toolbar) {
        builder.Section("Toolbar");
        builder.Stepper(
            "Height",
            std::format("{:.0f}", config.form.toolbar->height),
            [this](AppState&) -> void {
                EditWatch([this](ModularUiConfig& edited) -> void {
                    edited.form.toolbar->height = std::max(edited.form.toolbar->height - 2.0f, watch.MinimumToolbarHeight(edited));
                });
            },
            [this](AppState&) -> void {
                EditWatch([](ModularUiConfig& edited) -> void {
                    edited.form.toolbar->height = std::min(edited.form.toolbar->height + 2.0f, 160.0f);
                });
            },
            {}
        );
        const auto position = config.form.toolbar->position;
        builder.Row({
            {.text = "Above",
             .click = [this](AppState&) -> void {
                 EditWatch([](ModularUiConfig& edited) -> void { edited.form.toolbar->position = ToolbarPosition::Above; });
             },
             .selected = position == ToolbarPosition::Above},
            {.text = "Below",
             .click = [this](AppState&) -> void {
                 EditWatch([](ModularUiConfig& edited) -> void { edited.form.toolbar->position = ToolbarPosition::Below; });
             },
             .selected = position == ToolbarPosition::Below},
            {.text = "Remove", .click = [this](AppState&) -> void {
                 EditWatch([](ModularUiConfig& edited) -> void {
                     std::erase_if(edited.elements, [](const ModularElement& element) -> bool {
                         return ModularElements::RegionOf(element) == Region::Toolbar;
                     });
                     edited.form.toolbar.reset();
                 });
                 watch.selected.reset();
             }},
        });
    } else {
        builder.Button({.text = "Add a toolbar", .click = [this](AppState&) -> void {
                            EditWatch([](ModularUiConfig& edited) -> void { edited.form.toolbar = ToolbarLayout{}; });
                        }});
    }
    builder.Gap();
    builder.Gap();
    builder.Caption("Pick something on the preview to edit it");
}

auto Editor::BuildExtraFormProperties(PanelBuilder& builder, size_t form) -> void {
    const auto& extra = watch.Document().forms.at(form - 1);
    builder.Title(std::format("Form {}", form + 1));
    builder.Stepper(
        "Width",
        std::format("{:.0f}", extra.width),
        [this, form](AppState&) -> void {
            EditWatch([this, form](ModularUiConfig& edited) -> void {
                auto& changed = edited.forms.at(form - 1);
                changed.width = std::max(changed.width - 10.0f, watch.MinimumForm(edited, form).x());
            });
        },
        [this, form](AppState&) -> void {
            EditWatch([form](ModularUiConfig& edited) -> void { edited.forms.at(form - 1).width += 10.0f; });
        },
        {}
    );
    builder.Stepper(
        "Height",
        std::format("{:.0f}", extra.height),
        [this, form](AppState&) -> void {
            EditWatch([this, form](ModularUiConfig& edited) -> void {
                auto& changed = edited.forms.at(form - 1);
                changed.height = std::max(changed.height - 10.0f, watch.MinimumForm(edited, form).y());
            });
        },
        [this, form](AppState&) -> void {
            EditWatch([form](ModularUiConfig& edited) -> void { edited.forms.at(form - 1).height += 10.0f; });
        },
        {}
    );
    ColorRow(
        builder,
        "Colour",
        "Form colour",
        [form](const Editor& editor) -> std::string {
            return editor.watch.Document().forms.at(form - 1).bg_color.value_or("#00000000");
        },
        [form](Editor& editor, const std::string& color) -> void {
            editor.EditWatch([form, &color](ModularUiConfig& edited) -> void { edited.forms.at(form - 1).bg_color = color; });
        },
        [form](Editor& editor) -> void {
            editor.EditWatch([form](ModularUiConfig& edited) -> void { edited.forms.at(form - 1).bg_color.reset(); });
        }
    );
    builder.Caption("Drag it by an empty spot to move it");
    builder.Button({
        .text = "Delete form",
        .click = [this, form](AppState&) -> void {
            watch.RemoveForm(form);
            dialog_ = DialogKind::None;
            watch_apply_ = true;
            Touch();
        },
        .color = EditorColors::Danger(),
        .text_color = EditorColors::OnAccent(),
    });
}

auto Editor::BuildWatchProperties(PanelBuilder& builder, AppState& app, size_t index) -> void {
    const auto& element = watch.Document().elements.at(index);
    const auto edit = [this, index](std::function<void(ModularElement&)> change) -> Click {
        return [this, index, change = std::move(change)](AppState&) -> void {
            EditWatch([this, index, &change](ModularUiConfig& config) -> void {
                change(config.elements.at(index));
                watch.Fit(config, index);
            });
        };
    };
    builder.Title(PrettyType(element));
    if (!std::holds_alternative<PanelElement>(element) && !std::holds_alternative<OverlayListElement>(element)) {
        builder.Section("Content");
    }

    if (const auto* button = std::get_if<ButtonElement>(&element)) {
        TextRow(
            builder,
            app,
            "Text",
            "button.text",
            button->text,
            [index](Editor& editor, AppState&, const std::string& text) -> void {
                editor.EditWatch([&editor, index, &text](ModularUiConfig& config) -> void {
                    std::get<ButtonElement>(config.elements.at(index)).text = text;
                    editor.watch.Fit(config, index);
                });
            }
        );
    } else if (const auto* label = std::get_if<LabelElement>(&element)) {
        const auto* fixed = std::get_if<StaticLabelContent>(&label->content);
        const auto* clock = std::get_if<ClockLabelContent>(&label->content);
        const auto* zone_name = std::get_if<TimezoneLabelContent>(&label->content);
        const auto shown = watch.label_text ? watch.label_text(label->content) : std::string("Text");
        builder.Caption("Shows");
        builder.Row({
            {.text = "Text",
             .click = edit([shown](ModularElement& changed) -> void {
                 auto& item = std::get<LabelElement>(changed);
                 if (!std::holds_alternative<StaticLabelContent>(item.content)) {
                     item.content = StaticLabelContent{.text = shown};
                 }
             }),
             .selected = fixed != nullptr},
            {.text = "Clock",
             .click = edit([](ModularElement& changed) -> void {
                 auto& item = std::get<LabelElement>(changed);
                 if (const auto* name = std::get_if<TimezoneLabelContent>(&item.content)) {
                     item.content = ClockLabelContent{.format = "%H:%M", .timezone = name->timezone};
                 } else if (!std::holds_alternative<ClockLabelContent>(item.content)) {
                     item.content = ClockLabelContent{.format = "%H:%M"};
                 }
             }),
             .selected = clock != nullptr},
            {.text = "Zone name",
             .click = edit([](ModularElement& changed) -> void {
                 auto& item = std::get<LabelElement>(changed);
                 if (const auto* time = std::get_if<ClockLabelContent>(&item.content); time != nullptr && time->timezone) {
                     item.content = TimezoneLabelContent{.timezone = *time->timezone};
                 } else if (!std::holds_alternative<TimezoneLabelContent>(item.content)) {
                     item.content = TimezoneLabelContent{.timezone = LocalZone()};
                 }
             }),
             .weight = 1.4f,
             .selected = zone_name != nullptr},
        });
        builder.Button({
            .text = "Centered",
            .click = edit([](ModularElement& changed) -> void {
                auto& item = std::get<LabelElement>(changed);
                const float font = static_cast<float>(item.font_size);
                item.rect[1] += item.centered ? font * 0.75f + item.rect[3] / 2.0f : -(font * 0.75f + item.rect[3] / 2.0f);
                item.centered = !item.centered;
            }),
            .selected = label->centered,
        });
        if (zone_name != nullptr) {
            builder.Row({
                {.text = "Timezone", .weight = 1.0f, .text_color = EditorColors::Dim()},
                {.text = ZoneName(zone_name->timezone, app),
                 .click = [this, index](AppState&) -> void { OpenTimezones(index); },
                 .weight = 2.0f,
                 .selected = dialog_ == DialogKind::Timezone},
            });
        }
        if (fixed != nullptr) {
            TextRow(
                builder,
                app,
                "Text",
                "label.text",
                fixed->text,
                [index](Editor& editor, AppState&, const std::string& text) -> void {
                    editor.EditWatch([&editor, index, &text](ModularUiConfig& config) -> void {
                        std::get<LabelElement>(config.elements.at(index)).content = StaticLabelContent{.text = text};
                        editor.watch.Fit(config, index);
                    });
                }
            );
        } else if (clock != nullptr) {
            TextRow(
                builder,
                app,
                "Format",
                "label.format",
                clock->format,
                [index](Editor& editor, AppState&, const std::string& text) -> void {
                    editor.EditWatch([index, &text](ModularUiConfig& config) -> void {
                        auto& content = std::get<LabelElement>(config.elements.at(index)).content;
                        if (auto* clock_content = std::get_if<ClockLabelContent>(&content)) {
                            clock_content->format = text;
                        }
                    });
                }
            );
            std::vector<Cell> presets;
            for (const auto& [name, format] : {std::pair{"24h", "%H:%M"}, {"12h", "%I:%M %p"}, {"Date", "%x"}, {"Day", "%A"}}) {
                presets.push_back(
                    {.text = name,
                     .click = edit([format](ModularElement& changed) -> void {
                         auto& content = std::get<LabelElement>(changed).content;
                         if (auto* clock_content = std::get_if<ClockLabelContent>(&content)) {
                             clock_content->format = format;
                         }
                     }),
                     .selected = clock->format == format}
                );
            }
            builder.Row(std::move(presets));
            builder.Row({
                {.text = "Timezone", .weight = 1.0f, .text_color = EditorColors::Dim()},
                {.text = ZoneName(clock->timezone, app),
                 .click = [this, index](AppState&) -> void { OpenTimezones(index); },
                 .weight = 2.0f,
                 .selected = dialog_ == DialogKind::Timezone},
            });
            builder.Button({
                .text = "City name above",
                .click = edit([](ModularElement& changed) -> void {
                    if (auto* time = std::get_if<ClockLabelContent>(&std::get<LabelElement>(changed).content)) {
                        time->city_size = time->city_size ? std::nullopt : std::optional(14);
                    }
                }),
                .selected = clock->city_size.has_value(),
            });
            if (clock->city_size) {
                const auto change = [](int delta) -> std::function<void(ModularElement&)> {
                    return [delta](ModularElement& changed) -> void {
                        if (auto* time = std::get_if<ClockLabelContent>(&std::get<LabelElement>(changed).content);
                            time != nullptr && time->city_size) {
                            time->city_size = std::clamp(*time->city_size + delta, 6, 120);
                        }
                    };
                };
                builder.Stepper("City size", std::format("{}", *clock->city_size), edit(change(-1)), edit(change(1)), {});
            }
        }
    } else if (const auto* battery = std::get_if<BatteryListElement>(&element)) {
        builder.Stepper(
            "Devices",
            std::format("{}", battery->num_devices),
            edit([](ModularElement& changed) -> void {
                auto& item = std::get<BatteryListElement>(changed);
                item.num_devices = std::max<size_t>(item.num_devices, 2) - 1;
            }),
            edit([](ModularElement& changed) -> void { ++std::get<BatteryListElement>(changed).num_devices; }),
            {}
        );
    } else if (const auto* sprite = std::get_if<SpriteElement>(&element)) {
        builder.Caption(sprite->sprite);
    }

    builder.Section("Look");
    if (const auto font = ElementFont(element)) {
        builder.Stepper(
            "Font size",
            std::format("{}", *font),
            edit([](ModularElement& changed) -> void { SetElementFont(changed, ElementFont(changed).value_or(12) - 1); }),
            edit([](ModularElement& changed) -> void { SetElementFont(changed, ElementFont(changed).value_or(12) + 1); }),
            {}
        );
    }
    for (const bool background : {true, false}) {
        if (!ElementColor(element, background)) {
            continue;
        }
        ColorRow(
            builder,
            background ? "Colour" : "Text colour",
            background ? "Colour" : "Text colour",
            [index, background](const Editor& editor) -> std::string {
                return ElementColor(editor.watch.Document().elements.at(index), background).value_or("#ffffff");
            },
            [index, background](Editor& editor, const std::string& color) -> void {
                editor.EditWatch([index, background, &color](ModularUiConfig& config) -> void {
                    SetElementColor(config.elements.at(index), background, color);
                });
            },
            {}
        );
    }
    if (const auto radius = ElementRadius(element); radius && !std::holds_alternative<LabelElement>(element)) {
        const float shown = Theme::Resolve(
            *radius,
            Theme::Active().rounding,
            std::holds_alternative<PanelElement>(element) ? Shape::Panel : Shape::Control
        );
        builder.Stepper(
            "Corners",
            radius->has_value() ? std::format("{:.0f}", shown) : std::format("{:.0f} theme", shown),
            edit([shown](ModularElement& changed) -> void { SetElementRadius(changed, std::max(shown - 1.0f, 0.0f)); }),
            edit([shown](ModularElement& changed) -> void { SetElementRadius(changed, shown + 1.0f); }),
            {}
        );
        if (radius->has_value()) {
            builder.Button({.text = "Follow the theme", .click = edit([](ModularElement& changed) -> void {
                                                            SetElementRadius(changed, std::nullopt);
                                                        })});
        }
    }
    if (ModularElements::RegionOf(element) == Region::Toolbar || !std::holds_alternative<LabelElement>(element)
        || std::get<LabelElement>(element).centered) {
        const auto& rect = ModularElements::Rect(element);
        builder.Stepper(
            "Width",
            std::format("{:.0f}", rect[2]),
            edit([](ModularElement& changed) -> void { ModularElements::Rect(changed)[2] -= 2.0f; }),
            edit([](ModularElement& changed) -> void { ModularElements::Rect(changed)[2] += 2.0f; }),
            {}
        );
    }
    if (std::holds_alternative<ButtonElement>(element)) {
        builder.Section("Actions");
        std::vector<Cell> first;
        std::vector<Cell> second;
        for (const auto trigger :
             {ButtonTrigger::LeftClick, ButtonTrigger::RightClick, ButtonTrigger::ScrollUp, ButtonTrigger::ScrollDown}) {
            auto data = std::get<ButtonElement>(element).data;
            const auto& actions = TriggerActions(data, trigger);
            const size_t count = actions ? actions->size() : 0;
            auto name = std::string(TriggerName(trigger));
            name.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(name.front())));
            Cell cell{
                .text = count == 0 ? name : std::format("{} ({})", name, count),
                .click = [this, trigger](AppState&) -> void { OpenActions(trigger); },
                .selected = dialog_ == DialogKind::Actions && trigger_ == trigger,
            };
            (trigger == ButtonTrigger::LeftClick || trigger == ButtonTrigger::RightClick ? first : second)
                .push_back(std::move(cell));
        }
        builder.Row(std::move(first));
        builder.Row(std::move(second));
    }
    builder.Section("Arrange");
    builder.Row({
        {.text = "Forward",
         .click = [this, index](AppState&) -> void {
             watch.Restack(index, 1);
             watch_apply_ = true;
             Touch();
         }},
        {.text = "Back", .click = [this, index](AppState&) -> void {
             watch.Restack(index, -1);
             watch_apply_ = true;
             Touch();
         }},
    });
    builder.Row({
        {.text = "Duplicate",
         .click = [this, index](AppState&) -> void {
             watch.Duplicate(index);
             watch_apply_ = true;
             Touch();
         }},
        {.text = "Delete",
         .click = [this, index](AppState&) -> void {
             watch.Remove(index);
             dialog_ = DialogKind::None;
             watch_apply_ = true;
             Touch();
         },
         .color = EditorColors::Danger(),
         .text_color = EditorColors::OnAccent()},
    });
}

auto Editor::BuildKeyProperties(PanelBuilder& builder, AppState& app, size_t index) -> void {
    const auto& layout = keyboard.Document();
    const auto& key = layout.keys.at(index);
    const auto edit = [this, index](std::function<void(KeyDefinition&)> change) -> Click {
        return [this, index, change = std::move(change)](AppState&) -> void {
            EditKeyboard([index, &change](KeyboardLayout& edited) -> void { change(edited.keys.at(index)); });
        };
    };
    const auto* typed = std::get_if<TypeKey>(&key.action);
    const auto* action = std::get_if<ActionKey>(&key.action);
    const bool is_shortcut = action != nullptr && ShortcutOf(action->press) != nullptr;

    builder.Title(typed != nullptr ? std::format("Key {}", KeyCaption(typed->key)) : std::string("Key"));
    builder.Section("Action");
    builder.Row({
        {.text = "Key",
         .click = [this, index, typed](AppState&) -> void {
             if (typed == nullptr) {
                 EditKeyboard([index](KeyboardLayout& edited) -> void {
                     edited.keys.at(index).action = TypeKey{.key = VirtualKey::Space};
                 });
             }
             OpenKeyPicker();
         },
         .selected = typed != nullptr},
        {.text = "Macro",
         .click = [this, index, action, is_shortcut](AppState& state) -> void {
             if (action == nullptr || is_shortcut) {
                 EditKeyboard([index](KeyboardLayout& edited) -> void {
                     auto& changed = edited.keys.at(index);
                     changed.action = ActionKey{};
                     if (!changed.label) {
                         changed.label = std::vector<std::string>{"Macro"};
                     }
                 });
             }
             BeginText(state, "key.command", "", [index](Editor& editor, AppState&, const std::string& text) -> void {
                 editor.EditKeyboard([index, &text](KeyboardLayout& edited) -> void {
                     auto& changed = std::get<ActionKey>(edited.keys.at(index).action);
                     changed.press = CommandActions(text);
                 });
             });
         },
         .selected = action != nullptr && !is_shortcut},
        {.text = "Shortcut",
         .click = [this, index](AppState& state) -> void {
             BeginCapture(state, true, [index](Editor& editor, const std::vector<VirtualKey>& keys) -> void {
                 editor.EditKeyboard([index, &keys](KeyboardLayout& edited) -> void {
                     auto& changed = edited.keys.at(index);
                     changed.action = ActionKey{.press = {ShortcutButtonAction{.keys = keys}}};
                     if (!changed.label) {
                         changed.label = std::vector<std::string>{ShortcutText(keys)};
                     }
                 });
             });
         },
         .weight = 1.3f,
         .selected = is_shortcut},
    });
    builder.Row({
        {.text = "Layout",
         .click = edit([](KeyDefinition& changed) -> void { changed.action = ProfileKey{}; }),
         .selected = std::holds_alternative<ProfileKey>(key.action)},
        {.text = "Nothing",
         .click = edit([](KeyDefinition& changed) -> void { changed.action = BlankKey{}; }),
         .selected = std::holds_alternative<BlankKey>(key.action)},
    });
    if (typed != nullptr) {
        builder.Row({
            {.text = std::string(VirtualKeys::Name(typed->key)),
             .click = [this](AppState&) -> void { OpenKeyPicker(); },
             .selected = dialog_ == DialogKind::KeyPicker},
            {.text = "Press a key", .click = [this, index](AppState& state) -> void {
                 BeginCapture(state, false, [index](Editor& editor, const std::vector<VirtualKey>& keys) -> void {
                     editor.EditKeyboard([index, &keys](KeyboardLayout& edited) -> void {
                         auto& changed = edited.keys.at(index);
                         changed.action = TypeKey{.key = keys.back()};
                         changed.label.reset();
                         changed.shifted_label.reset();
                     });
                 });
             }},
        });
    } else if (action != nullptr && is_shortcut) {
        builder.Caption(std::format("Sends {}", ShortcutText(ShortcutOf(action->press)->keys)));
    } else if (action != nullptr) {
        TextRow(
            builder,
            app,
            "Command",
            "key.command",
            JoinCommand(CommandOf(action->press).value_or(std::vector<std::string>{})),
            [index](Editor& editor, AppState&, const std::string& text) -> void {
                editor.EditKeyboard([index, &text](KeyboardLayout& edited) -> void {
                    if (auto* changed = std::get_if<ActionKey>(&edited.keys.at(index).action)) {
                        changed->press = CommandActions(text);
                    }
                });
            }
        );
        TextRow(
            builder,
            app,
            "On release (optional)",
            "key.release",
            JoinCommand(CommandOf(action->release).value_or(std::vector<std::string>{})),
            [index](Editor& editor, AppState&, const std::string& text) -> void {
                editor.EditKeyboard([index, &text](KeyboardLayout& edited) -> void {
                    if (auto* changed = std::get_if<ActionKey>(&edited.keys.at(index).action)) {
                        changed->release = CommandActions(text);
                    }
                });
            }
        );
    }
    builder.Section("Label");
    const auto labels = layout.Labels(key);
    TextRow(
        builder,
        app,
        key.label ? "Label" : "Label (automatic)",
        "key.label",
        labels.empty() ? std::string() : labels.front(),
        [index](Editor& editor, AppState&, const std::string& text) -> void {
            editor.EditKeyboard([index, &text](KeyboardLayout& edited) -> void {
                edited.keys.at(index).label = std::vector<std::string>{text};
            });
        }
    );
    TextRow(
        builder,
        app,
        "Shifted label",
        "key.shifted",
        key.shifted_label.value_or(""),
        [index](Editor& editor, AppState&, const std::string& text) -> void {
            editor.EditKeyboard([index, &text](KeyboardLayout& edited) -> void {
                edited.keys.at(index).shifted_label = text.empty() ? std::nullopt : std::optional(text);
            });
        }
    );
    if (key.label || key.shifted_label) {
        builder.Button({.text = "Automatic labels", .click = edit([](KeyDefinition& changed) -> void {
                                                        changed.label.reset();
                                                        changed.shifted_label.reset();
                                                    })});
    }
    builder.Section("Look");
    builder.Stepper(
        "Width",
        std::format("{:g}", key.width),
        [this, index](AppState& state) -> void {
            if (!keyboard.Resize(index, -kKeyStep)) {
                Notify(state, "Keys need at least a quarter unit");
            }
            Touch();
        },
        [this, index](AppState& state) -> void {
            if (!keyboard.Resize(index, kKeyStep)) {
                Notify(state, "No room to grow, move the next key first");
            }
            Touch();
        },
        {}
    );
    ColorRow(
        builder,
        "Key colour",
        "Key colour",
        [index](const Editor& editor) -> std::string {
            return editor.keyboard.Document().keys.at(index).style.bg_color.value_or("@panel");
        },
        [index](Editor& editor, const std::string& color) -> void {
            editor.EditKeyboard([index, &color](KeyboardLayout& edited) -> void { edited.keys.at(index).style.bg_color = color; });
        },
        [index](Editor& editor) -> void {
            editor.EditKeyboard([index](KeyboardLayout& edited) -> void { edited.keys.at(index).style.bg_color.reset(); });
        }
    );
    ColorRow(
        builder,
        "Text colour",
        "Text colour",
        [index](const Editor& editor) -> std::string {
            return editor.keyboard.Document().keys.at(index).style.fg_color.value_or("@text");
        },
        [index](Editor& editor, const std::string& color) -> void {
            editor.EditKeyboard([index, &color](KeyboardLayout& edited) -> void { edited.keys.at(index).style.fg_color = color; });
        },
        [index](Editor& editor) -> void {
            editor.EditKeyboard([index](KeyboardLayout& edited) -> void { edited.keys.at(index).style.fg_color.reset(); });
        }
    );
    const float corners = Theme::Resolve(key.style.corner_radius, Theme::Active().rounding, Shape::Key);
    builder.Stepper(
        "Corners",
        key.style.corner_radius ? std::format("{:.0f}", corners) : std::format("{:.0f} theme", corners),
        edit([corners](KeyDefinition& changed) -> void { changed.style.corner_radius = std::max(corners - 1.0f, 0.0f); }),
        edit([corners](KeyDefinition& changed) -> void { changed.style.corner_radius = corners + 1.0f; }),
        {}
    );
    builder.Section("Arrange");
    builder.Row({
        {.text = "Duplicate",
         .click = [this, index](AppState& state) -> void {
             if (!keyboard.Duplicate(index)) {
                 Notify(state, "No free spot, add a column or row first");
             }
             Touch();
         }},
        {.text = "Delete",
         .click = [this, index](AppState&) -> void {
             keyboard.Remove(index);
             dialog_ = DialogKind::None;
             Touch();
         },
         .color = EditorColors::Danger(),
         .text_color = EditorColors::OnAccent()},
    });
}

auto Editor::BuildKeyboardProperties(PanelBuilder& builder, AppState& app) -> void {
    const auto& layout = keyboard.Document();
    builder.Title("Keyboard");
    builder.Section("Keyboards");
    {
        const auto& profiles = KeyboardProfiles::All();
        for (size_t start = 0; start < profiles.size(); start += 2) {
            std::vector<Cell> cells;
            for (size_t i = start; i < std::min(start + 2, profiles.size()); ++i) {
                const auto name = profiles[i].name;
                cells.push_back({
                    .text = name,
                    .click = [this, name](AppState& state) -> void { SelectKeyboard(state, name); },
                    .selected = name == keyboard_file_name_,
                });
            }
            if (cells.size() < 2) {
                cells.push_back({.text = ""});
            }
            builder.Row(std::move(cells), 30.0f);
        }
        std::vector<Cell> actions{{.text = "Save as new", .click = [this](AppState& state) -> void { SaveAsNew(state); }}};
        if (!KeyboardProfiles::IsBuiltIn(layout.name) && !layout.source.empty()) {
            actions.push_back({
                .text = delete_armed_ ? "Sure?" : "Delete",
                .click = [this](AppState& state) -> void { DeleteKeyboard(state); },
                .color = EditorColors::Danger(),
                .text_color = EditorColors::OnAccent(),
            });
        }
        builder.Row(std::move(actions));
    }
    builder.Section("Settings");
    TextRow(builder, app, "Name", "keyboard.name", layout.name, [](Editor& editor, AppState&, const std::string& text) -> void {
        if (!text.empty()) {
            editor.EditKeyboard([&text](KeyboardLayout& edited) -> void { edited.name = text; });
        }
    });
    builder.Caption(std::format("{:g} x {} grid, {} keys", layout.columns, layout.rows, layout.keys.size()));
    TextRow(
        builder,
        app,
        "Region (like fi or de)",
        "keyboard.region",
        layout.region.value_or(""),
        [](Editor& editor, AppState&, const std::string& text) -> void {
            editor.EditKeyboard([&text](KeyboardLayout& edited) -> void {
                edited.region = text.empty() ? std::nullopt : std::optional(text);
            });
        }
    );
    builder.Caption("Purple pointer holds");
    for (size_t start = 0; start < kAltModifiers.size(); start += 3) {
        std::vector<Cell> cells;
        for (size_t i = start; i < start + 3; ++i) {
            const auto [modifier, name] = kAltModifiers.at(i);
            cells.push_back({
                .text = std::string(name),
                .click = [this, modifier](AppState&) -> void {
                    EditKeyboard([modifier](KeyboardLayout& edited) -> void { edited.alt_modifier = modifier; });
                },
                .selected = layout.alt_modifier == modifier,
            });
        }
        builder.Row(std::move(cells));
    }
    builder.Button({
        .text = "Labels follow the region",
        .click = [this](AppState&) -> void {
            EditKeyboard([](KeyboardLayout& edited) -> void {
                const bool automatic = edited.auto_labels.value_or(true);
                edited.auto_labels = automatic ? std::optional(false) : std::nullopt;
            });
        },
        .selected = layout.auto_labels.value_or(true),
    });
    builder.Gap();
    builder.Caption("Pick a key, or press + to add one");
}

auto Editor::HsvColor(float hue, float saturation, float value) -> std::string {
    return HsvHex(hue, saturation, value);
}

auto Editor::EnsureColorImages(AppState& app, const ColorDialog& dialog) -> void {
    constexpr uint32_t kWheel = 192;
    constexpr uint32_t kBrightness = 256;
    constexpr uint32_t kBrightnessHeight = 8;
    if (!wheel_image_ || std::abs(wheel_value_ - dialog.value) > 1e-3f) {
        std::vector<std::byte> pixels(size_t{kWheel} * kWheel * 4);
        const float radius = static_cast<float>(kWheel) / 2.0f;
        for (uint32_t y = 0; y < kWheel; ++y) {
            for (uint32_t x = 0; x < kWheel; ++x) {
                const float dx = static_cast<float>(x) + 0.5f - radius;
                const float dy = static_cast<float>(y) + 0.5f - radius;
                const float distance = std::sqrt(dx * dx + dy * dy);
                const float hue = std::atan2(dy, dx) / (2.0f * std::numbers::pi_v<float>);
                const auto rgb = HsvRgb(hue, std::min(distance / (radius - 1.0f), 1.0f), dialog.value);
                const size_t offset = (size_t{y} * kWheel + x) * 4;
                pixels[offset] = ToByte(rgb[0]);
                pixels[offset + 1] = ToByte(rgb[1]);
                pixels[offset + 2] = ToByte(rgb[2]);
                pixels[offset + 3] = ToByte(radius + 0.5f - distance);
            }
        }
        wheel_image_ = UploadPixels(app, kWheel, kWheel, pixels);
        wheel_value_ = dialog.value;
    }
    const Eigen::Vector2f key(dialog.hue, dialog.saturation);
    if (!brightness_image_ || (key - brightness_key_).norm() > 1e-3f) {
        std::vector<std::byte> pixels(size_t{kBrightness} * kBrightnessHeight * 4);
        for (uint32_t x = 0; x < kBrightness; ++x) {
            const auto rgb = HsvRgb(dialog.hue, dialog.saturation, static_cast<float>(x) / static_cast<float>(kBrightness - 1));
            for (uint32_t y = 0; y < kBrightnessHeight; ++y) {
                const size_t offset = (size_t{y} * kBrightness + x) * 4;
                pixels[offset] = ToByte(rgb[0]);
                pixels[offset + 1] = ToByte(rgb[1]);
                pixels[offset + 2] = ToByte(rgb[2]);
                pixels[offset + 3] = std::byte{255};
            }
        }
        brightness_image_ = UploadPixels(app, kBrightness, kBrightnessHeight, pixels);
        brightness_key_ = key;
    }
}

auto Editor::PickColor(const Eigen::Vector2f& point) -> void {
    if (!color_) {
        return;
    }
    auto& dialog = *color_;
    if (color_drag_ == ColorDrag::Wheel) {
        const float radius = wheel_rect_.w / 2.0f;
        const Eigen::Vector2f offset = point - Eigen::Vector2f(wheel_rect_.x + radius, wheel_rect_.y + radius);
        const float hue = std::atan2(offset.y(), offset.x()) / (2.0f * std::numbers::pi_v<float>);
        dialog.hue = hue < 0.0f ? hue + 1.0f : hue;
        dialog.saturation = std::clamp(offset.norm() / radius, 0.0f, 1.0f);
    } else if (color_drag_ == ColorDrag::Brightness) {
        dialog.value = std::clamp((point.x() - brightness_rect_.x) / brightness_rect_.w, 0.0f, 1.0f);
    } else {
        return;
    }
    dialog.set(*this, HsvHex(dialog.hue, dialog.saturation, dialog.value));
    amend_ = true;
    Touch();
}

auto Editor::DialogPointer(AppState&, const Eigen::Vector2f& point, bool pressed) -> bool {
    if (dialog_ != DialogKind::Color || !color_) {
        return false;
    }
    const auto& dialog = *color_;
    if (!pressed) {
        const bool dragging = color_drag_ != ColorDrag::None;
        color_drag_ = ColorDrag::None;
        amend_ = false;
        if (dragging) {
            const auto current = dialog.get(*this);
            std::erase(recent_colors_, current);
            recent_colors_.push_front(current);
            if (recent_colors_.size() > kRecentColors) {
                recent_colors_.pop_back();
            }
            Touch();
        }
        return dragging;
    }
    const float radius = wheel_rect_.w / 2.0f;
    const Eigen::Vector2f center(wheel_rect_.x + radius, wheel_rect_.y + radius);
    if ((point - center).norm() <= radius + 2.0f) {
        color_drag_ = ColorDrag::Wheel;
    } else if (
        point.x() >= brightness_rect_.x - 4.0f && point.x() <= brightness_rect_.x + brightness_rect_.w + 4.0f
        && point.y() >= brightness_rect_.y - 6.0f && point.y() <= brightness_rect_.y + brightness_rect_.h + 6.0f
    ) {
        color_drag_ = ColorDrag::Brightness;
    } else {
        return false;
    }
    amend_ = false;
    PickColor(point);
    return true;
}

auto Editor::DialogHover(AppState&, const Eigen::Vector2f& point) -> bool {
    if (color_drag_ == ColorDrag::None) {
        return false;
    }
    PickColor(point);
    return true;
}

auto Editor::BuildColorDialog(PanelBuilder& builder, AppState& app) -> void {
    if (!color_) {
        return;
    }
    auto& dialog = *color_;
    const auto current = dialog.get(*this);
    const auto pick = [this](AppState&, const std::string& color) -> void {
        if (!color_) {
            return;
        }
        auto& picked = *color_;
        picked.set(*this, color);
        const auto [hue, saturation, value] = HexHsv(color);
        picked.hue = hue;
        picked.saturation = saturation;
        picked.value = value;
        std::erase(recent_colors_, color);
        recent_colors_.push_front(color);
        if (recent_colors_.size() > kRecentColors) {
            recent_colors_.pop_back();
        }
        Touch();
    };
    builder.Title(dialog.title);
    builder.Row(
        {{.text = current,
          .color = EditorColors::Hex(current),
          .text_color = EditorColors::Readable(EditorColors::Hex(current)),
          .rim = true}},
        36.0f
    );

    EnsureColorImages(app, dialog);
    constexpr float kWheelSize = 180.0f;
    auto& canvas = builder.Target();
    wheel_rect_ = {(builder.Width() - kWheelSize) / 2.0f, builder.Y(), kWheelSize, kWheelSize};
    if (wheel_image_) {
        auto& wheel = canvas.Sprite(wheel_rect_.x, wheel_rect_.y, wheel_rect_.w, wheel_rect_.h);
        wheel.fg_color = Eigen::Vector4f::Ones();
        wheel.SetSprite(wheel_image_);
    }
    const float radius = kWheelSize / 2.0f;
    const float angle = dialog.hue * 2.0f * std::numbers::pi_v<float>;
    const Eigen::Vector2f marker = Eigen::Vector2f(wheel_rect_.x + radius, wheel_rect_.y + radius)
                                 + Eigen::Vector2f(std::cos(angle), std::sin(angle)) * dialog.saturation * radius;
    canvas.Decoration(
        marker.x() - 7.0f,
        marker.y() - 7.0f,
        14.0f,
        14.0f,
        7.0f,
        std::make_unique<OutlineControl>(std::nullopt, HighlightOutline{.color = Eigen::Vector4f::Ones(), .width = 2.5f})
    );
    builder.Gap(kWheelSize + PanelBuilder::kGap);

    brightness_rect_ = {PanelBuilder::kPadding, builder.Y() + 4.0f, builder.Width() - 2.0f * PanelBuilder::kPadding, 16.0f};
    if (brightness_image_) {
        auto& bar = canvas.Sprite(brightness_rect_.x, brightness_rect_.y, brightness_rect_.w, brightness_rect_.h);
        bar.fg_color = Eigen::Vector4f::Ones();
        bar.SetSprite(brightness_image_);
    }
    const float knob = brightness_rect_.x + dialog.value * brightness_rect_.w;
    canvas.Decoration(
        knob - 4.0f,
        brightness_rect_.y - 4.0f,
        8.0f,
        brightness_rect_.h + 8.0f,
        3.0f,
        std::make_unique<OutlineControl>(std::nullopt, HighlightOutline{.color = Eigen::Vector4f::Ones(), .width = 2.0f})
    );
    builder.Gap(brightness_rect_.h + 8.0f + PanelBuilder::kGap);

    builder.Caption(dialog.tokens ? "Theme, follows theme changes" : "Theme");
    std::vector<std::string> palette;
    for (const auto& color : Theme::Active().colors) {
        palette.push_back(dialog.tokens ? Theme::Token(color.name) : color.hex);
    }
    builder.Swatches(palette, current, pick, 8);
    if (!recent_colors_.empty()) {
        builder.Caption("Recent");
        const std::vector<std::string> recent(recent_colors_.begin(), recent_colors_.end());
        builder.Swatches(recent, current, pick, 8);
    }
    builder.Caption("Hue");
    std::vector<Cell> hues;
    constexpr int kHues = 12;
    for (int i = 0; i < kHues; ++i) {
        const float hue = static_cast<float>(i) / kHues;
        hues.push_back({
            .text = std::abs(hue - dialog.hue) < 0.5f / kHues ? "*" : "",
            .click = [this, hue](AppState&) -> void {
                if (!color_) {
                    return;
                }
                auto& picked = *color_;
                picked.hue = hue;
                picked.saturation = std::max(picked.saturation, 0.5f);
                picked.value = std::max(picked.value, 0.5f);
                Touch();
            },
            .color = EditorColors::Hex(HsvHex(hue, 0.7f, 0.95f)),
            .text_color = EditorColors::OnAccent(),
        });
    }
    builder.Row(std::move(hues), 26.0f);
    builder.Caption("Shade");
    std::vector<std::string> shades;
    for (const float value : {1.0f, 0.8f, 0.6f, 0.4f}) {
        for (const float saturation : {0.15f, 0.35f, 0.55f, 0.75f, 0.95f}) {
            shades.push_back(HsvHex(dialog.hue, saturation, value));
        }
    }
    builder.Swatches(shades, current, pick, 5);
    std::vector<Cell> footer;
    if (dialog.reset) {
        footer.push_back({.text = "Theme default", .click = [this](AppState&) -> void {
                              if (color_) {
                                  color_->reset(*this);
                              }
                              Touch();
                          }});
    }
    footer.push_back({.text = "Done", .click = [this](AppState&) -> void { CloseDialog(); }});
    builder.Row(std::move(footer));
}

auto Editor::BuildActionsDialog(PanelBuilder& builder, AppState&) -> void {
    if (target == EditTarget::Keyboard || !watch.selected || *watch.selected >= watch.Document().elements.size()) {
        return;
    }
    const size_t index = *watch.selected;
    const auto* button = std::get_if<ButtonElement>(&watch.Document().elements.at(index));
    if (button == nullptr) {
        return;
    }
    const auto trigger = trigger_;
    const auto change = [index, trigger](Editor& editor, const std::function<void(std::vector<ButtonAction>&)>& apply) -> void {
        editor.EditWatch([index, trigger, &apply](ModularUiConfig& config) -> void {
            auto& actions = TriggerActions(std::get<ButtonElement>(config.elements.at(index)).data, trigger);
            if (!actions) {
                actions.emplace();
            }
            apply(*actions);
            if (actions->empty()) {
                actions.reset();
            }
        });
    };
    auto data = button->data;
    const auto actions = TriggerActions(data, trigger).value_or(std::vector<ButtonAction>{});
    builder.Title(std::format("On {}", TriggerName(trigger)));
    if (actions.empty()) {
        builder.Caption("Nothing yet");
    }
    for (size_t i = 0; i < actions.size(); ++i) {
        const auto& action = actions[i];
        Cell summary{.text = Describe(action), .weight = 4.0f};
        const auto id = std::format("action.{}", i);
        const bool focused = focus_ && focus_->id == id;
        if (focused) {
            summary.text = std::format("{}_", focus_->buffer);
            summary.color = EditorColors::Surface();
            summary.text_color = EditorColors::Accent();
        }
        if (const auto* exec = std::get_if<ExecButtonAction>(&action)) {
            summary.click = [this, i, id, change, initial = JoinCommand(exec->command)](AppState& state) -> void {
                BeginText(state, id, initial, [i, change](Editor& editor, AppState&, const std::string& text) -> void {
                    change(editor, [i, &text](std::vector<ButtonAction>& edited) -> void {
                        edited.at(i) = ExecButtonAction{.command = SplitCommand(text)};
                    });
                });
            };
        } else if (const auto* typed = std::get_if<TypeTextButtonAction>(&action)) {
            summary.click = [this, i, id, change, initial = typed->text](AppState& state) -> void {
                BeginText(state, id, initial, [i, change](Editor& editor, AppState&, const std::string& text) -> void {
                    change(editor, [i, &text](std::vector<ButtonAction>& edited) -> void {
                        edited.at(i) = TypeTextButtonAction{.text = text};
                    });
                });
            };
        }
        builder.Row({
            std::move(summary),
            {.text = "x",
             .click = [this, i, change](AppState&) -> void {
                 change(*this, [i](std::vector<ButtonAction>& edited) -> void {
                     edited.erase(edited.begin() + static_cast<std::ptrdiff_t>(i));
                 });
             },
             .color = EditorColors::Danger(),
             .text_color = EditorColors::OnAccent()},
        });
    }
    builder.Gap();
    builder.Caption("Add");
    const auto add = [this, change](ButtonAction action) -> Click {
        return [this, change, action = std::move(action)](AppState&) -> void {
            change(*this, [&action](std::vector<ButtonAction>& edited) -> void { edited.push_back(action); });
        };
    };
    const size_t next = actions.size();
    builder.Row({
        {.text = "Macro",
         .click = [this, change, next](AppState& state) -> void {
             change(*this, [](std::vector<ButtonAction>& edited) -> void { edited.emplace_back(ExecButtonAction{}); });
             BeginText(
                 state,
                 std::format("action.{}", next),
                 "",
                 [next, change](Editor& editor, AppState&, const std::string& text) -> void {
                     change(editor, [next, &text](std::vector<ButtonAction>& edited) -> void {
                         if (next < edited.size()) {
                             edited.at(next) = ExecButtonAction{.command = SplitCommand(text)};
                         }
                     });
                 }
             );
         }},
        {.text = "Type text", .click = [this, change, next](AppState& state) -> void {
             change(*this, [](std::vector<ButtonAction>& edited) -> void { edited.emplace_back(TypeTextButtonAction{}); });
             BeginText(
                 state,
                 std::format("action.{}", next),
                 "",
                 [next, change](Editor& editor, AppState&, const std::string& text) -> void {
                     change(editor, [next, &text](std::vector<ButtonAction>& edited) -> void {
                         if (next < edited.size()) {
                             edited.at(next) = TypeTextButtonAction{.text = text};
                         }
                     });
                 }
             );
         }},
    });
    builder.Row({
        {.text = "Shortcut",
         .click = [this, change](AppState& state) -> void {
             BeginCapture(state, true, [change](Editor& editor, const std::vector<VirtualKey>& keys) -> void {
                 change(editor, [&keys](std::vector<ButtonAction>& edited) -> void {
                     edited.emplace_back(ShortcutButtonAction{.keys = keys});
                 });
             });
         }},
        {.text = "Keyboard",
         .click = add(OverlayButtonAction{.target = std::string(kKeyboardName), .action = OverlayToggleVisible{}})},
    });
    builder.Row({
        {.text = "Recenter", .click = add(SystemButtonAction{.action = SystemAction::Recenter})},
        {.text = "Show/hide", .click = add(SystemButtonAction{.action = SystemAction::ShowHide})},
    });
    builder.Row({
        {.text = "Next layout", .click = add(KeyboardProfileButtonAction{})},
        {.text = "Editor", .click = add(SystemButtonAction{.action = SystemAction::Edit})},
    });
    builder.Gap();
    builder.Button({.text = "Done", .click = [this](AppState&) -> void { CloseDialog(); }});
}

auto Editor::BuildTimezonePicker(PanelBuilder& builder, AppState& app) -> void {
    if (target != EditTarget::Watch || timezone_element_ >= watch.Document().elements.size()) {
        return;
    }
    const auto* label = std::get_if<LabelElement>(&watch.Document().elements.at(timezone_element_));
    const auto* clock = label != nullptr ? std::get_if<ClockLabelContent>(&label->content) : nullptr;
    const auto* zone_name = label != nullptr ? std::get_if<TimezoneLabelContent>(&label->content) : nullptr;
    if (clock == nullptr && zone_name == nullptr) {
        return;
    }
    const size_t index = timezone_element_;
    const auto choose = [this, index](std::optional<std::string> zone) -> Click {
        return [this, index, zone = std::move(zone)](AppState& state) -> void {
            EditWatch([this, index, &zone](ModularUiConfig& config) -> void {
                auto& content = std::get<LabelElement>(config.elements.at(index)).content;
                if (auto* clock_content = std::get_if<ClockLabelContent>(&content)) {
                    clock_content->timezone = zone ? std::optional<std::variant<size_t, std::string>>(*zone) : std::nullopt;
                } else if (auto* name_content = std::get_if<TimezoneLabelContent>(&content)) {
                    name_content->timezone = zone.value_or(LocalZone());
                }
                watch.Fit(config, index);
            });
            EndText(state);
            CloseDialog();
        };
    };
    builder.Title("Timezone");
    const bool searching = focus_ && focus_->id == "timezone.search";
    const std::string query = searching ? focus_->buffer : timezone_query_;
    TextRow(
        builder,
        app,
        "Search, like helsinki or america",
        "timezone.search",
        timezone_query_,
        [](Editor& editor, AppState&, const std::string& text) -> void { editor.timezone_query_ = text; }
    );
    const auto current = clock != nullptr ? ZoneName(clock->timezone, app) : ZoneName(zone_name->timezone, app);
    builder.Button({.text = "Local", .click = choose(std::nullopt), .selected = clock != nullptr && !clock->timezone.has_value()});
    const auto needle = Searchable(query);
    size_t shown = 0;
    size_t matches = 0;
    for (const auto& zone : AllZones()) {
        if (!needle.empty() && !Searchable(zone).contains(needle)) {
            continue;
        }
        ++matches;
        if (shown >= kZoneResults) {
            continue;
        }
        auto display = zone;
        std::ranges::replace(display, '_', ' ');
        builder.Row({{.text = display, .click = choose(zone), .selected = display == current}}, 28.0f);
        ++shown;
    }
    if (matches > shown) {
        builder.Caption(std::format("{} more, keep typing to narrow it down", matches - shown));
    } else if (matches == 0) {
        builder.Caption("No timezone matches that");
    }
    builder.Button({.text = "Done", .click = [this](AppState& state) -> void {
                        EndText(state);
                        CloseDialog();
                    }});
}

auto Editor::BuildThemeProperties(PanelBuilder& builder, AppState& app) -> void {
    const auto& current = theme.Current();
    builder.Title("Theme");
    builder.Section("Themes");
    const auto& themes = Theme::All();
    for (size_t start = 0; start < themes.size(); start += 2) {
        std::vector<Cell> cells;
        for (size_t i = start; i < std::min(start + 2, themes.size()); ++i) {
            const auto name = themes[i].name;
            cells.push_back({
                .text = name,
                .click = [this, name](AppState& state) -> void { SelectTheme(state, name); },
                .selected = name == current.name,
            });
        }
        if (cells.size() < 2) {
            cells.push_back({.text = ""});
        }
        builder.Row(std::move(cells), 30.0f);
    }
    std::vector<Cell> actions{{.text = "New theme", .click = [this](AppState& state) -> void { NewTheme(state); }}};
    if (!Theme::IsBuiltIn(current.name) && !current.source.empty()) {
        actions.push_back({
            .text = delete_armed_ ? "Sure?" : "Delete",
            .click = [this](AppState& state) -> void { DeleteTheme(state); },
            .color = EditorColors::Danger(),
            .text_color = EditorColors::OnAccent(),
        });
    }
    builder.Row(std::move(actions));
    if (!Theme::IsBuiltIn(current.name)) {
        TextRow(
            builder,
            app,
            "Name",
            "theme.name",
            current.name,
            [](Editor& editor, AppState& state, const std::string& text) -> void { editor.RenameTheme(state, text); }
        );
    }

    builder.Section("Corners");
    std::vector<Cell> roundings;
    roundings.reserve(kRoundings.size());
    for (const auto rounding : kRoundings) {
        roundings.push_back({
            .text = std::string(Theme::Name(rounding)),
            .click = [this, rounding](AppState&) -> void {
                EditTheme([rounding](ThemeDefinition& edited) -> void { edited.rounding = rounding; });
            },
            .selected = current.rounding == rounding,
        });
    }
    builder.Row(std::move(roundings));

    builder.Section("Colours");
    builder.Caption("Elements using @name follow these");
    for (size_t start = 0; start < current.colors.size(); start += 2) {
        std::vector<Cell> cells;
        for (size_t i = start; i < std::min(start + 2, current.colors.size()); ++i) {
            const auto& color = current.colors[i];
            const auto swatch = EditorColors::Hex(color.hex);
            const auto name = color.name;
            cells.push_back({
                .text = name,
                .click = [this, name](AppState&) -> void {
                    const auto hex = theme.Current().Lookup(name).value_or("#ffffff");
                    const auto [hue, saturation, value] = HexHsv(hex);
                    OpenColor({
                        .title = Theme::Token(name),
                        .get = [name](const Editor& editor) -> std::string {
                            return editor.theme.Current().Lookup(name).value_or("#ffffff");
                        },
                        .set = [name](Editor& editor, const std::string& picked) -> void {
                            editor.EditTheme([&name, &picked](ThemeDefinition& edited) -> void {
                                for (auto& entry : edited.colors) {
                                    if (entry.name == name) {
                                        entry.hex = picked;
                                    }
                                }
                            });
                        },
                        .hue = hue,
                        .saturation = saturation,
                        .value = value,
                        .tokens = false,
                    });
                },
                .color = swatch,
                .text_color = EditorColors::Readable(swatch),
                .rim = true,
            });
        }
        if (cells.size() < 2) {
            cells.push_back({.text = ""});
        }
        builder.Row(std::move(cells), 30.0f);
    }
}

auto Editor::BuildKeyPicker(PanelBuilder& builder, AppState&) -> void {
    if (target == EditTarget::Watch || !keyboard.selected || *keyboard.selected >= keyboard.Document().keys.size()) {
        return;
    }
    const size_t index = *keyboard.selected;
    const auto* typed = std::get_if<TypeKey>(&keyboard.Document().keys.at(index).action);
    builder.Title("Pick a key");
    for (size_t start = 0; start < kKeyCategories.size(); start += 3) {
        std::vector<Cell> cells;
        for (size_t i = start; i < std::min(start + 3, kKeyCategories.size()); ++i) {
            cells.push_back({
                .text = std::string(kKeyCategories.at(i).name),
                .click = [this, i](AppState&) -> void {
                    key_category_ = i;
                    Touch();
                },
                .selected = key_category_ == i,
            });
        }
        builder.Row(std::move(cells), 30.0f);
    }
    builder.Gap();
    const auto keys = kKeyCategories.at(std::min(key_category_, kKeyCategories.size() - 1)).keys;
    for (size_t start = 0; start < keys.size(); start += 4) {
        std::vector<Cell> cells;
        for (size_t i = start; i < std::min(start + 4, keys.size()); ++i) {
            const auto key = keys[i];
            cells.push_back({
                .text = KeyCaption(key),
                .click = [this, index, key](AppState&) -> void {
                    EditKeyboard([index, key](KeyboardLayout& edited) -> void {
                        auto& changed = edited.keys.at(index);
                        changed.action = TypeKey{.key = key};
                        changed.label.reset();
                        changed.shifted_label.reset();
                    });
                },
                .selected = typed != nullptr && typed->key == key,
            });
        }
        while (cells.size() < 4) {
            cells.push_back({.text = ""});
        }
        builder.Row(std::move(cells), 30.0f);
    }
    builder.Gap();
    builder.Button({.text = "Done", .click = [this](AppState&) -> void { CloseDialog(); }});
}
