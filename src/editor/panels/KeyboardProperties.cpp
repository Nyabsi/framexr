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

#include "panels/KeyboardProperties.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "ButtonComponent.hpp"
#include "ChoiceGridComponent.hpp"
#include "ColorFieldComponent.hpp"
#include "Editor.hpp"
#include "LaserInputComponent.hpp"
#include "RowComponent.hpp"
#include "SectionComponent.hpp"
#include "StackComponent.hpp"
#include "StepperComponent.hpp"
#include "SwatchComponent.hpp"
#include "TextLineComponent.hpp"
#include "panels/ActionsPanel.hpp"
#include "panels/KeyPickerPanel.hpp"
#include "panels/PanelStyle.hpp"

namespace {

constexpr auto kAltModifiers = std::to_array<std::pair<AltModifier, std::string_view>>({
    {AltModifier::None, "None"},
    {AltModifier::Shift, "Shift"},
    {AltModifier::Ctrl, "Ctrl"},
    {AltModifier::Alt, "Alt"},
    {AltModifier::Super, "Super"},
    {AltModifier::Meta, "AltGr"},
});

auto CommandOf(const std::vector<ButtonAction>& actions) -> std::vector<std::string> {
    for (const auto& action : actions) {
        if (const auto* exec = std::get_if<ExecButtonAction>(&action)) {
            return exec->command;
        }
    }
    return {};
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
    auto command = ActionsPanel::SplitCommand(text);
    if (command.empty()) {
        return {};
    }
    return {ExecButtonAction{.command = std::move(command)}};
}

} // namespace

KeyboardProperties::KeyboardProperties(Editor& editor, const Eigen::Vector2f& size)
    : FormComponent("keyboard"),
      m_stack(AddComponent<StackComponent>("stack", PanelStyle::kPadding, PanelStyle::kGap)) {
    SetSize(size);
    m_stack.SetSize(size);
    m_stack.SetBgColor(FormTheme::Rgba("@panel"));
    const auto text = FormTheme::Rgba("@text");
    const auto& design = editor.KeyboardDesign();
    const auto selected = design.Selected();
    if (!selected || *selected >= design.Document().Keys().size()) {
        m_stack.AddComponent<TextLineComponent>("title", "Keyboard", PanelStyle::kTitleFont, text, PanelStyle::kTitleHeight);
        AddKeyboards(editor);
        AddSettings(editor);
        return;
    }
    const auto* typed = std::get_if<TypeKey>(&design.Document().Keys().at(*selected).action);
    const auto title = typed != nullptr ? std::format("Key {}", KeyPickerPanel::Caption(typed->key)) : std::string("Key");
    m_stack.AddComponent<TextLineComponent>("title", title, PanelStyle::kTitleFont, text, PanelStyle::kTitleHeight);
    AddKeyAction(editor, *selected);
    AddKeyLabel(editor, *selected);
    AddKeyLook(editor, *selected);
    AddKeyArrange(editor, *selected);
}

auto KeyboardProperties::AddKeyboards(Editor& editor) -> void {
    const auto& layout = editor.KeyboardDesign().Document();

    m_stack.AddComponent<SectionComponent>("keyboards", "Keyboards");

    std::vector<std::string> names;
    std::optional<size_t> selected;
    for (const auto& profile : KeyboardLayout::All()) {
        if (profile.Name() == editor.KeyboardFile()) {
            selected = names.size();
        }
        names.push_back(profile.Name());
    }
    auto& list = m_stack.AddComponent<ChoiceGridComponent>("list", names, selected, 2, PanelStyle::kListRow);
    list.SetOnChoose([&editor, names](size_t index) -> void { editor.SelectKeyboard(names.at(index)); });

    auto& actions = m_stack.AddComponent<RowComponent>("actions", PanelStyle::kRowHeight, PanelStyle::kGap);

    auto& copy = actions.AddComponent<ButtonComponent>("save-as-new", "Save as new");
    copy.SetFontSize(PanelStyle::kTextFont);
    copy.SetFgColor(FormTheme::Rgba("@text"));
    copy.SetBgColor(FormTheme::Rgba("@surface"));
    copy.SetEllipsize(true);
    copy.SetOnRelease([&editor]() -> void { editor.SaveAsNew(); });

    if (!KeyboardLayout::IsBuiltIn(layout.Name()) && !layout.Source().empty()) {
        auto& remove = actions.AddComponent<ButtonComponent>("delete", editor.DeleteArmed() ? "Sure?" : "Delete");
        remove.SetFontSize(PanelStyle::kTextFont);
        remove.SetFgColor(FormTheme::Rgba("@on-accent"));
        remove.SetBgColor(FormTheme::Rgba("@red"));
        remove.SetEllipsize(true);
        remove.SetOnRelease([&editor]() -> void { editor.DeleteKeyboard(); });
    }
}

auto KeyboardProperties::AddSettings(Editor& editor) -> void {
    const auto& layout = editor.KeyboardDesign().Document();
    const auto subtle = FormTheme::Rgba("@subtle");
    const auto text = FormTheme::Rgba("@text");
    const auto surface = FormTheme::Rgba("@surface");
    const auto accent = FormTheme::Rgba("@accent");
    const auto on_accent = FormTheme::Rgba("@on-accent");

    m_stack.AddComponent<SectionComponent>("settings", "Settings");

    m_stack.AddComponent<LaserInputComponent>(
        "keyboard.name",
        "Name",
        editor.Text(),
        layout.Name(),
        [&editor](const std::string& typed) -> void {
            if (!typed.empty()) {
                editor.EditKeyboard([&typed](KeyboardLayout& edited) -> void { edited.SetName(typed); });
            }
        }
    );

    const auto grid = std::format("{:g} x {} grid, {} keys", layout.Columns(), layout.Rows(), layout.Keys().size());
    m_stack.AddComponent<TextLineComponent>("grid", grid, PanelStyle::kCaptionFont, subtle, PanelStyle::kCaptionHeight);

    m_stack.AddComponent<LaserInputComponent>(
        "keyboard.region",
        "Region (like fi or de)",
        editor.Text(),
        layout.Region().value_or(""),
        [&editor](const std::string& typed) -> void {
            editor.EditKeyboard([&typed](KeyboardLayout& edited) -> void {
                edited.SetRegion(typed.empty() ? std::nullopt : std::optional(typed));
            });
        }
    );

    m_stack.AddComponent<TextLineComponent>(
        "alt-hint",
        "Purple pointer holds",
        PanelStyle::kCaptionFont,
        subtle,
        PanelStyle::kCaptionHeight
    );
    std::vector<std::string> modifiers;
    std::optional<size_t> held;
    for (const auto& [modifier, name] : kAltModifiers) {
        if (layout.AltKey() == modifier) {
            held = modifiers.size();
        }
        modifiers.emplace_back(name);
    }
    auto& alt = m_stack.AddComponent<ChoiceGridComponent>("alt", modifiers, held, 3, PanelStyle::kRowHeight);
    alt.SetOnChoose([&editor](size_t index) -> void {
        editor.EditKeyboard([index](KeyboardLayout& edited) -> void { edited.SetAltKey(kAltModifiers.at(index).first); });
    });

    const bool automatic = layout.AutoLabels().value_or(true);
    auto& follow = m_stack.AddComponent<ButtonComponent>("auto-labels", "Labels follow the region");
    follow.SetSize({0.0f, PanelStyle::kRowHeight});
    follow.SetFontSize(PanelStyle::kTextFont);
    follow.SetFgColor(automatic ? on_accent : text);
    follow.SetBgColor(automatic ? accent : surface);
    follow.SetEllipsize(true);
    follow.SetOnRelease([&editor]() -> void {
        editor.EditKeyboard([](KeyboardLayout& edited) -> void {
            const bool following = edited.AutoLabels().value_or(true);
            edited.SetAutoLabels(following ? std::optional(false) : std::nullopt);
        });
    });

    m_stack.AddComponent<FormComponent>("spacer");
    m_stack.AddComponent<TextLineComponent>(
        "hint",
        "Pick a key, or press + to add one",
        PanelStyle::kCaptionFont,
        subtle,
        PanelStyle::kCaptionHeight
    );
}

auto KeyboardProperties::AddKeyAction(Editor& editor, size_t index) -> void {
    const auto& key = editor.KeyboardDesign().Document().Keys().at(index);
    const auto text = FormTheme::Rgba("@text");
    const auto subtle = FormTheme::Rgba("@subtle");
    const auto surface = FormTheme::Rgba("@surface");
    const auto accent = FormTheme::Rgba("@accent");
    const auto on_accent = FormTheme::Rgba("@on-accent");
    const auto* typed = std::get_if<TypeKey>(&key.action);
    const auto* action = std::get_if<ActionKey>(&key.action);
    const auto* shortcut = action != nullptr ? ShortcutOf(action->press) : nullptr;
    const bool is_shortcut = shortcut != nullptr;
    const bool is_macro = action != nullptr && !is_shortcut;
    const bool is_layout = std::holds_alternative<ProfileKey>(key.action);
    const bool is_blank = std::holds_alternative<BlankKey>(key.action);
    const bool is_ime = std::holds_alternative<ImeKey>(key.action);

    m_stack.AddComponent<SectionComponent>("action", "Action");

    auto& kinds = m_stack.AddComponent<RowComponent>("kinds", PanelStyle::kRowHeight, PanelStyle::kGap);
    kinds.SetWeights({1.0f, 1.0f, 1.3f});

    auto& as_key = kinds.AddComponent<ButtonComponent>("key", "Key");
    as_key.SetFontSize(PanelStyle::kTextFont);
    as_key.SetFgColor(typed != nullptr ? on_accent : text);
    as_key.SetBgColor(typed != nullptr ? accent : surface);
    as_key.SetEllipsize(true);
    as_key.SetOnRelease([&editor, index, convert = typed == nullptr]() -> void {
        if (convert) {
            editor.EditKeyboard([index](KeyboardLayout& edited) -> void {
                edited.Keys().at(index).action = TypeKey{.key = VirtualKey::Space};
            });
        }
        editor.OpenKeyPicker();
    });

    auto& as_macro = kinds.AddComponent<ButtonComponent>("macro", "Macro");
    as_macro.SetFontSize(PanelStyle::kTextFont);
    as_macro.SetFgColor(is_macro ? on_accent : text);
    as_macro.SetBgColor(is_macro ? accent : surface);
    as_macro.SetEllipsize(true);
    as_macro.SetOnRelease([&editor, index, convert = !is_macro]() -> void {
        if (convert) {
            editor.EditKeyboard([index](KeyboardLayout& edited) -> void {
                auto& changed = edited.Keys().at(index);
                changed.action = ActionKey{};
                if (!changed.label) {
                    changed.label = std::vector<std::string>{"Macro"};
                }
            });
        }
        editor.Text().Begin("key.command", "", [&editor, index](const std::string& command) -> void {
            editor.EditKeyboard([index, &command](KeyboardLayout& edited) -> void {
                std::get<ActionKey>(edited.Keys().at(index).action).press = CommandActions(command);
            });
        });
    });

    auto& as_shortcut = kinds.AddComponent<ButtonComponent>("shortcut", "Shortcut");
    as_shortcut.SetFontSize(PanelStyle::kTextFont);
    as_shortcut.SetFgColor(is_shortcut ? on_accent : text);
    as_shortcut.SetBgColor(is_shortcut ? accent : surface);
    as_shortcut.SetEllipsize(true);
    as_shortcut.SetOnRelease([&editor, index]() -> void {
        editor.Text().Capture(true, [&editor, index](const std::vector<VirtualKey>& keys) -> void {
            editor.EditKeyboard([index, &keys](KeyboardLayout& edited) -> void {
                auto& changed = edited.Keys().at(index);
                changed.action = ActionKey{.press = {ShortcutButtonAction{.keys = keys}}};
                if (!changed.label) {
                    changed.label = std::vector<std::string>{VirtualKeys::Chord(keys)};
                }
            });
        });
    });

    auto& others = m_stack.AddComponent<RowComponent>("others", PanelStyle::kRowHeight, PanelStyle::kGap);

    auto& as_layout = others.AddComponent<ButtonComponent>("layout", "Layout");
    as_layout.SetFontSize(PanelStyle::kTextFont);
    as_layout.SetFgColor(is_layout ? on_accent : text);
    as_layout.SetBgColor(is_layout ? accent : surface);
    as_layout.SetEllipsize(true);
    as_layout.SetOnRelease([&editor, index]() -> void {
        editor.EditKeyboard([index](KeyboardLayout& edited) -> void { edited.Keys().at(index).action = ProfileKey{}; });
    });

    auto& as_ime = others.AddComponent<ButtonComponent>("ime", "IME");
    as_ime.SetFontSize(PanelStyle::kTextFont);
    as_ime.SetFgColor(is_ime ? on_accent : text);
    as_ime.SetBgColor(is_ime ? accent : surface);
    as_ime.SetEllipsize(true);
    as_ime.SetOnRelease([&editor, index]() -> void {
        editor.EditKeyboard([index](KeyboardLayout& edited) -> void { edited.Keys().at(index).action = ImeKey{}; });
    });

    auto& as_blank = others.AddComponent<ButtonComponent>("nothing", "Nothing");
    as_blank.SetFontSize(PanelStyle::kTextFont);
    as_blank.SetFgColor(is_blank ? on_accent : text);
    as_blank.SetBgColor(is_blank ? accent : surface);
    as_blank.SetEllipsize(true);
    as_blank.SetOnRelease([&editor, index]() -> void {
        editor.EditKeyboard([index](KeyboardLayout& edited) -> void { edited.Keys().at(index).action = BlankKey{}; });
    });

    if (typed != nullptr) {
        const bool picking = editor.Dialog() == DialogKind::KeyPicker;
        auto& row = m_stack.AddComponent<RowComponent>("typed", PanelStyle::kRowHeight, PanelStyle::kGap);

        auto& pick = row.AddComponent<ButtonComponent>("pick", VirtualKeys::Name(typed->key));
        pick.SetFontSize(PanelStyle::kTextFont);
        pick.SetFgColor(picking ? on_accent : text);
        pick.SetBgColor(picking ? accent : surface);
        pick.SetEllipsize(true);
        pick.SetOnRelease([&editor]() -> void { editor.OpenKeyPicker(); });

        auto& press = row.AddComponent<ButtonComponent>("press", "Press a key");
        press.SetFontSize(PanelStyle::kTextFont);
        press.SetFgColor(text);
        press.SetBgColor(surface);
        press.SetEllipsize(true);
        press.SetOnRelease([&editor, index]() -> void {
            editor.Text().Capture(false, [&editor, index](const std::vector<VirtualKey>& keys) -> void {
                editor.EditKeyboard([index, &keys](KeyboardLayout& edited) -> void {
                    auto& changed = edited.Keys().at(index);
                    changed.action = TypeKey{.key = keys.back()};
                    changed.label.reset();
                    changed.shifted_label.reset();
                });
            });
        });
    } else if (shortcut != nullptr) {
        const auto sends = std::format("Sends {}", VirtualKeys::Chord(shortcut->keys));
        m_stack.AddComponent<TextLineComponent>("sends", sends, PanelStyle::kCaptionFont, subtle, PanelStyle::kCaptionHeight);
    } else if (action != nullptr) {
        m_stack.AddComponent<LaserInputComponent>(
            "key.command",
            "Command",
            editor.Text(),
            ActionsPanel::JoinCommand(CommandOf(action->press)),
            [&editor, index](const std::string& command) -> void {
                editor.EditKeyboard([index, &command](KeyboardLayout& edited) -> void {
                    if (auto* changed = std::get_if<ActionKey>(&edited.Keys().at(index).action)) {
                        changed->press = CommandActions(command);
                    }
                });
            }
        );
        m_stack.AddComponent<LaserInputComponent>(
            "key.release",
            "On release (optional)",
            editor.Text(),
            ActionsPanel::JoinCommand(CommandOf(action->release)),
            [&editor, index](const std::string& command) -> void {
                editor.EditKeyboard([index, &command](KeyboardLayout& edited) -> void {
                    if (auto* changed = std::get_if<ActionKey>(&edited.Keys().at(index).action)) {
                        changed->release = CommandActions(command);
                    }
                });
            }
        );
    }
}

auto KeyboardProperties::AddKeyLabel(Editor& editor, size_t index) -> void {
    const auto& layout = editor.KeyboardDesign().Document();
    const auto& key = layout.Keys().at(index);
    const auto labels = layout.Labels(key);

    m_stack.AddComponent<SectionComponent>("label", "Label");

    m_stack.AddComponent<LaserInputComponent>(
        "key.label",
        key.label ? "Label" : "Label (automatic)",
        editor.Text(),
        labels.empty() ? std::string() : labels.front(),
        [&editor, index](const std::string& typed) -> void {
            editor.EditKeyboard([index, &typed](KeyboardLayout& edited) -> void {
                edited.Keys().at(index).label = std::vector<std::string>{typed};
            });
        }
    );

    m_stack.AddComponent<LaserInputComponent>(
        "key.shifted",
        "Shifted label",
        editor.Text(),
        key.shifted_label.value_or(""),
        [&editor, index](const std::string& typed) -> void {
            editor.EditKeyboard([index, &typed](KeyboardLayout& edited) -> void {
                edited.Keys().at(index).shifted_label = typed.empty() ? std::nullopt : std::optional(typed);
            });
        }
    );

    if (key.label || key.shifted_label) {
        auto& automatic = m_stack.AddComponent<ButtonComponent>("automatic", "Automatic labels");
        automatic.SetSize({0.0f, PanelStyle::kRowHeight});
        automatic.SetFontSize(PanelStyle::kTextFont);
        automatic.SetFgColor(FormTheme::Rgba("@text"));
        automatic.SetBgColor(FormTheme::Rgba("@surface"));
        automatic.SetEllipsize(true);
        automatic.SetOnRelease([&editor, index]() -> void {
            editor.EditKeyboard([index](KeyboardLayout& edited) -> void {
                edited.Keys().at(index).label.reset();
                edited.Keys().at(index).shifted_label.reset();
            });
        });
    }
}

auto KeyboardProperties::AddKeyLook(Editor& editor, size_t index) -> void {
    const auto& key = editor.KeyboardDesign().Document().Keys().at(index);

    m_stack.AddComponent<SectionComponent>("look", "Look");

    auto& width = m_stack.AddComponent<StepperComponent>("width", "Width", std::format("{:g}", key.width));
    width.Minus().SetOnRelease([&editor, index]() -> void {
        if (!editor.KeyboardDesign().Resize(index, -kKeyStep)) {
            Editor::Notify("Keys need at least a quarter unit");
        }
        editor.Touch();
    });
    width.Plus().SetOnRelease([&editor, index]() -> void {
        if (!editor.KeyboardDesign().Resize(index, kKeyStep)) {
            Editor::Notify("No room to grow, move the next key first");
        }
        editor.Touch();
    });

    auto& background = m_stack.AddComponent<ColorFieldComponent>("key-colour", "Key colour", key.style.bg_color.value_or("@panel"));
    background.Swatch().SetOnRelease([&editor, index]() -> void {
        editor.OpenColor({
            .title = "Key colour",
            .get = [&editor, index]() -> std::string {
                return editor.KeyboardDesign().Document().Keys().at(index).style.bg_color.value_or("@panel");
            },
            .set = [&editor, index](const std::string& color) -> void {
                editor.EditKeyboard([index, &color](KeyboardLayout& edited) -> void {
                    edited.Keys().at(index).style.bg_color = color;
                });
            },
            .reset = [&editor, index]() -> void {
                editor.EditKeyboard([index](KeyboardLayout& edited) -> void { edited.Keys().at(index).style.bg_color.reset(); });
            },
        });
    });

    auto& foreground = m_stack.AddComponent<ColorFieldComponent>("text-colour", "Text colour", key.style.fg_color.value_or("@text"));
    foreground.Swatch().SetOnRelease([&editor, index]() -> void {
        editor.OpenColor({
            .title = "Text colour",
            .get = [&editor, index]() -> std::string {
                return editor.KeyboardDesign().Document().Keys().at(index).style.fg_color.value_or("@text");
            },
            .set = [&editor, index](const std::string& color) -> void {
                editor.EditKeyboard([index, &color](KeyboardLayout& edited) -> void {
                    edited.Keys().at(index).style.fg_color = color;
                });
            },
            .reset = [&editor, index]() -> void {
                editor.EditKeyboard([index](KeyboardLayout& edited) -> void { edited.Keys().at(index).style.fg_color.reset(); });
            },
        });
    });

    const float corners = FormTheme::Resolve(key.style.corner_radius, g_themes->Active().rounding, Shape::Key);
    const auto shown = key.style.corner_radius ? std::format("{:.0f}", corners) : std::format("{:.0f} theme", corners);
    auto& corner = m_stack.AddComponent<StepperComponent>("corners", "Corners", shown);
    corner.Minus().SetOnRelease([&editor, index, corners]() -> void {
        editor.EditKeyboard([index, corners](KeyboardLayout& edited) -> void {
            edited.Keys().at(index).style.corner_radius = std::max(corners - 1.0f, 0.0f);
        });
    });
    corner.Plus().SetOnRelease([&editor, index, corners]() -> void {
        editor.EditKeyboard([index, corners](KeyboardLayout& edited) -> void {
            edited.Keys().at(index).style.corner_radius = corners + 1.0f;
        });
    });
}

auto KeyboardProperties::AddKeyArrange(Editor& editor, size_t index) -> void {
    m_stack.AddComponent<SectionComponent>("arrange", "Arrange");

    auto& row = m_stack.AddComponent<RowComponent>("arrange-row", PanelStyle::kRowHeight, PanelStyle::kGap);

    auto& duplicate = row.AddComponent<ButtonComponent>("duplicate", "Duplicate");
    duplicate.SetFontSize(PanelStyle::kTextFont);
    duplicate.SetFgColor(FormTheme::Rgba("@text"));
    duplicate.SetBgColor(FormTheme::Rgba("@surface"));
    duplicate.SetEllipsize(true);
    duplicate.SetOnRelease([&editor, index]() -> void {
        if (!editor.KeyboardDesign().Duplicate(index)) {
            Editor::Notify("No free spot, add a column or row first");
        }
        editor.Touch();
    });

    auto& remove = row.AddComponent<ButtonComponent>("delete", "Delete");
    remove.SetFontSize(PanelStyle::kTextFont);
    remove.SetFgColor(FormTheme::Rgba("@on-accent"));
    remove.SetBgColor(FormTheme::Rgba("@red"));
    remove.SetEllipsize(true);
    remove.SetOnRelease([&editor, index]() -> void {
        editor.KeyboardDesign().Remove(index);
        editor.CloseDialog();
    });
}
