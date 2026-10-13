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

#include "panels/KeyPickerPanel.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

#include "ButtonComponent.hpp"
#include "ChoiceGridComponent.hpp"
#include "Editor.hpp"
#include "KeyboardLayout.hpp"
#include "StackComponent.hpp"
#include "TextLineComponent.hpp"
#include "panels/PanelStyle.hpp"

namespace {

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

constexpr auto kKeyCategories = std::to_array<KeyGroup>({
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

} // namespace

auto KeyPickerPanel::Caption(VirtualKey key) -> std::string {
    const KeyboardLayout empty;
    std::string caption;
    for (const auto& label : empty.Labels({.action = TypeKey{.key = key}})) {
        if (!caption.empty()) {
            caption += ' ';
        }
        caption += label;
    }
    return caption.empty() ? std::string(VirtualKeys::Name(key)) : caption;
}

KeyPickerPanel::KeyPickerPanel(Editor& editor, const Eigen::Vector2f& size)
    : FormComponent("key-picker"),
      m_stack(AddComponent<StackComponent>("stack", PanelStyle::kPadding, PanelStyle::kGap)) {
    SetSize(size);
    m_stack.SetSize(size);
    m_stack.SetBgColor(FormTheme::Rgba("@panel"));
    const auto& keyboard = editor.KeyboardDesign();
    const auto selected = keyboard.Selected();
    if (editor.Target() == EditTarget::Watch || !selected || *selected >= keyboard.Document().Keys().size()) {
        return;
    }
    const size_t index = *selected;
    const auto* typed = std::get_if<TypeKey>(&keyboard.Document().Keys().at(index).action);

    m_stack.AddComponent<TextLineComponent>(
        "title",
        "Pick a key",
        PanelStyle::kTitleFont,
        FormTheme::Rgba("@text"),
        PanelStyle::kTitleHeight
    );

    std::vector<std::string> categories;
    for (const auto& category : kKeyCategories) {
        categories.emplace_back(category.name);
    }
    const size_t category = std::min(editor.KeyCategory(), kKeyCategories.size() - 1);
    auto& tabs = m_stack.AddComponent<ChoiceGridComponent>("categories", categories, category, 3, PanelStyle::kListRow);
    tabs.SetOnChoose([&editor](size_t chosen) -> void { editor.SetKeyCategory(chosen); });

    m_stack.AddComponent<FormComponent>("spacer");

    const auto keys = kKeyCategories.at(category).keys;
    std::vector<std::string> captions;
    std::optional<size_t> current;
    for (const auto key : keys) {
        if (typed != nullptr && typed->key == key) {
            current = captions.size();
        }
        captions.push_back(Caption(key));
    }
    auto& grid = m_stack.AddComponent<ChoiceGridComponent>("keys", captions, current, 4, PanelStyle::kListRow);
    grid.SetOnChoose([&editor, index, keys](size_t chosen) -> void {
        const auto key = keys[chosen];
        editor.EditKeyboard([index, key](KeyboardLayout& edited) -> void {
            auto& changed = edited.Keys().at(index);
            changed.action = TypeKey{.key = key};
            changed.label.reset();
            changed.shifted_label.reset();
        });
    });

    m_stack.AddComponent<FormComponent>("spacer");

    auto& done = m_stack.AddComponent<ButtonComponent>("done", "Done");
    done.SetSize({0.0f, PanelStyle::kRowHeight});
    done.SetFontSize(PanelStyle::kTextFont);
    done.SetFgColor(FormTheme::Rgba("@text"));
    done.SetBgColor(FormTheme::Rgba("@surface"));
    done.SetEllipsize(true);
    done.SetOnRelease([&editor]() -> void { editor.CloseDialog(); });
}
