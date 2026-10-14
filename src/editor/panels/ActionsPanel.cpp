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

#include "panels/ActionsPanel.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <utility>

#include "ButtonComponent.hpp"
#include "Editor.hpp"
#include "RowComponent.hpp"
#include "StackComponent.hpp"
#include "TextLineComponent.hpp"
#include "panels/PanelStyle.hpp"
#include "views/Keyboard.hpp"

namespace
{

constexpr size_t kSummaryLength = 26;

constexpr auto kTriggers = std::to_array<TriggerSlot>({
    {ButtonTrigger::Press, "press", &ButtonData::click_down},
    {ButtonTrigger::Click, "click", &ButtonData::click_up},
    {ButtonTrigger::LongClick, "long click", &ButtonData::long_click_up},
    {ButtonTrigger::RightPress, "right press", &ButtonData::right_down},
    {ButtonTrigger::RightClick, "right click", &ButtonData::right_up},
    {ButtonTrigger::LongRightClick, "long right click", &ButtonData::long_right_up},
    {ButtonTrigger::MiddlePress, "middle press", &ButtonData::middle_down},
    {ButtonTrigger::MiddleClick, "middle click", &ButtonData::middle_up},
    {ButtonTrigger::LongMiddleClick, "long middle click", &ButtonData::long_middle_up},
    {ButtonTrigger::ScrollUp, "scroll up", &ButtonData::scroll_up},
    {ButtonTrigger::ScrollDown, "scroll down", &ButtonData::scroll_down},
});

auto Shorten(std::string text) -> std::string
{
    if (text.size() > kSummaryLength)
    {
        text.resize(kSummaryLength - 3);
        text += "...";
    }
    return text;
}

} // namespace

auto ActionsPanel::Describe(const ButtonAction& action) -> std::string
{
    return std::visit(
        [](const auto& item) -> std::string
        {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, ExecButtonAction>)
            {
                return Shorten(std::format("Run {}", JoinCommand(item.command)));
            }
            else if constexpr (std::is_same_v<Item, VirtualKeyButtonAction>)
            {
                return std::format("Key {}", VirtualKeys::Name(item.keycode));
            }
            else if constexpr (std::is_same_v<Item, WatchButtonAction>)
            {
                return "Watch setting";
            }
            else if constexpr (std::is_same_v<Item, OverlayButtonAction>)
            {
                if (const auto* name = std::get_if<std::string>(&item.target))
                {
                    return Shorten(std::format("Toggle {}", *name == kKeyboardName ? "keyboard" : *name));
                }
                return "Toggle screen";
            }
            else if constexpr (std::is_same_v<Item, WindowButtonAction>)
            {
                return Shorten(std::format("{} {}", item.action == WindowAction::Destroy ? "Close" : "Open", item.target));
            }
            else if constexpr (std::is_same_v<Item, ToastButtonAction>)
            {
                return Shorten(std::format("Notify {}", item.message));
            }
            else if constexpr (std::is_same_v<Item, ColorAdjustButtonAction>)
            {
                return "Colour gain";
            }
            else if constexpr (std::is_same_v<Item, DragMultiplierButtonAction>)
            {
                return "Drag speed";
            }
            else if constexpr (std::is_same_v<Item, SystemButtonAction>)
            {
                switch (item.action)
                {
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
            }
            else if constexpr (std::is_same_v<Item, SendOscButtonAction>)
            {
                return Shorten(std::format("OSC {}", item.parameter));
            }
            else if constexpr (std::is_same_v<Item, ShortcutButtonAction>)
            {
                return Shorten(VirtualKeys::Chord(item.keys));
            }
            else if constexpr (std::is_same_v<Item, TypeTextButtonAction>)
            {
                return Shorten(std::format("Type {}", item.text));
            }
            else
            {
                return "Next layout";
            }
        },
        action
    );
}

auto ActionsPanel::Triggers() -> std::span<const TriggerSlot>
{
    return kTriggers;
}

auto ActionsPanel::TriggerName(ButtonTrigger trigger) -> std::string_view
{
    return std::ranges::find(kTriggers, trigger, &TriggerSlot::trigger)->name;
}

auto ActionsPanel::ActionsOf(ButtonData& data, ButtonTrigger trigger) -> std::optional<std::vector<ButtonAction>>&
{
    return data.*std::ranges::find(kTriggers, trigger, &TriggerSlot::trigger)->actions;
}

auto ActionsPanel::SplitCommand(std::string_view text) -> std::vector<std::string>
{
    std::vector<std::string> parts;
    std::string current;
    bool quoted = false;
    bool active = false;
    for (const char c : text)
    {
        if (c == '"')
        {
            quoted = !quoted;
            active = true;
        }
        else if (c == ' ' && !quoted)
        {
            if (active)
            {
                parts.push_back(std::move(current));
                current.clear();
                active = false;
            }
        }
        else
        {
            current += c;
            active = true;
        }
    }
    if (active)
    {
        parts.push_back(std::move(current));
    }
    return parts;
}

auto ActionsPanel::JoinCommand(const std::vector<std::string>& command) -> std::string
{
    std::string text;
    for (const auto& part : command)
    {
        if (!text.empty())
        {
            text += ' ';
        }
        text += part.empty() || part.contains(' ') ? std::format("\"{}\"", part) : part;
    }
    return text;
}

ActionsPanel::ActionsPanel(Editor& editor, const Eigen::Vector2f& size)
    : FormComponent("actions"),
      m_editor(editor),
      m_trigger(editor.Trigger()),
      m_stack(AddComponent<StackComponent>("stack", PanelStyle::kPadding, PanelStyle::kGap))
{
    SetSize(size);
    m_stack.SetSize(size);
    m_stack.SetBgColor(FormTheme::Rgba("@panel"));
    const auto& watch = editor.WatchDesign();
    const auto selected = watch.Selected();
    if (editor.Target() == EditTarget::Keyboard || !selected || *selected >= watch.Document().Components().size())
    {
        return;
    }
    const auto* button = std::get_if<ButtonElement>(&watch.Document().Components().at(*selected));
    if (button == nullptr)
    {
        return;
    }
    m_element = *selected;
    auto data = button->data;
    const auto actions = ActionsOf(data, m_trigger).value_or(std::vector<ButtonAction>{});

    const auto title = std::format("On {}", TriggerName(m_trigger));
    m_stack.AddComponent<TextLineComponent>(
        "title",
        title,
        PanelStyle::kTitleFont,
        FormTheme::Rgba("@text"),
        PanelStyle::kTitleHeight
    );
    if (actions.empty())
    {
        m_stack.AddComponent<TextLineComponent>(
            "empty",
            "Nothing yet",
            PanelStyle::kCaptionFont,
            FormTheme::Rgba("@subtle"),
            PanelStyle::kCaptionHeight
        );
    }
    for (size_t i = 0; i < actions.size(); ++i)
    {
        AddAction(actions[i], i);
    }
    m_stack.AddComponent<FormComponent>("spacer");
    m_stack.AddComponent<TextLineComponent>(
        "add",
        "Add",
        PanelStyle::kCaptionFont,
        FormTheme::Rgba("@subtle"),
        PanelStyle::kCaptionHeight
    );
    AddChoices();
    m_stack.AddComponent<FormComponent>("spacer");

    auto& done = m_stack.AddComponent<ButtonComponent>("done", "Done");
    done.SetSize({0.0f, PanelStyle::kRowHeight});
    done.SetFontSize(PanelStyle::kTextFont);
    done.SetFgColor(FormTheme::Rgba("@text"));
    done.SetBgColor(FormTheme::Rgba("@surface"));
    done.SetEllipsize(true);
    done.SetOnRelease([&editor]() -> void { editor.CloseDialog(); });
}

auto ActionsPanel::Change(Editor& editor, size_t element, ButtonTrigger trigger, const ActionsChange& apply) -> void
{
    editor.EditWatch(
        [element, trigger, &apply](FormDocument& document) -> void
        {
            auto& actions = ActionsOf(std::get<ButtonElement>(document.Components().at(element)).data, trigger);
            if (!actions)
            {
                actions.emplace();
            }
            apply(*actions);
            if (actions->empty())
            {
                actions.reset();
            }
        }
    );
}

auto ActionsPanel::AddAction(const ButtonAction& action, size_t position) -> void
{
    const auto id = std::format("action.{}", position);
    const auto& entry = m_editor.Text();
    const bool focused = entry.Editing(id);
    const auto summary = focused ? std::format("{}_", entry.Buffer()) : Describe(action);

    auto& row = m_stack.AddComponent<RowComponent>(id, PanelStyle::kRowHeight, PanelStyle::kGap);
    row.SetWeights({4.0f, 1.0f});

    const auto* exec = std::get_if<ExecButtonAction>(&action);
    const auto* typed = std::get_if<TypeTextButtonAction>(&action);
    if (exec != nullptr || typed != nullptr)
    {
        auto& field = row.AddComponent<ButtonComponent>("summary", summary);
        field.SetFontSize(PanelStyle::kTextFont);
        field.SetFgColor(FormTheme::Rgba(focused ? "@accent" : "@text"));
        field.SetBgColor(FormTheme::Rgba("@surface"));
        field.SetEllipsize(true);
        const auto initial = exec != nullptr ? JoinCommand(exec->command) : typed->text;
        const bool is_exec = exec != nullptr;
        field.SetOnRelease(
            [&editor = m_editor, element = m_element, trigger = m_trigger, id, initial, position, is_exec]() -> void
            {
                editor.Text().Begin(
                    id,
                    initial,
                    [&editor, element, trigger, position, is_exec](const std::string& text) -> void
                    {
                        Change(
                            editor,
                            element,
                            trigger,
                            [position, is_exec, &text](std::vector<ButtonAction>& edited) -> void
                            {
                                if (is_exec)
                                {
                                    edited.at(position) = ExecButtonAction{.command = SplitCommand(text)};
                                }
                                else
                                {
                                    edited.at(position) = TypeTextButtonAction{.text = text};
                                }
                            }
                        );
                    }
                );
            }
        );
    }
    else
    {
        row.AddComponent<TextLineComponent>("summary", summary, PanelStyle::kTextFont, FormTheme::Rgba("@text"));
    }

    auto& remove = row.AddComponent<ButtonComponent>("remove", "x");
    remove.SetFontSize(PanelStyle::kTextFont);
    remove.SetFgColor(FormTheme::Rgba("@on-accent"));
    remove.SetBgColor(FormTheme::Rgba("@red"));
    remove.SetEllipsize(true);
    remove.SetOnRelease(
        [&editor = m_editor, element = m_element, trigger = m_trigger, position]() -> void
        {
            Change(
                editor,
                element,
                trigger,
                [position](std::vector<ButtonAction>& edited) -> void
                { edited.erase(edited.begin() + static_cast<std::ptrdiff_t>(position)); }
            );
        }
    );
}

auto ActionsPanel::AddChoices() -> void
{
    const auto text = FormTheme::Rgba("@text");
    const auto surface = FormTheme::Rgba("@surface");
    const size_t next = [this]() -> size_t
    {
        auto data = std::get<ButtonElement>(m_editor.WatchDesign().Document().Components().at(m_element)).data;
        const auto& actions = ActionsOf(data, m_trigger);
        return actions ? actions->size() : 0;
    }();

    auto& first = m_stack.AddComponent<RowComponent>("typed", PanelStyle::kRowHeight, PanelStyle::kGap);

    auto& macro = first.AddComponent<ButtonComponent>("macro", "Macro");
    macro.SetFontSize(PanelStyle::kTextFont);
    macro.SetFgColor(text);
    macro.SetBgColor(surface);
    macro.SetEllipsize(true);
    macro.SetOnRelease(
        [&editor = m_editor, element = m_element, trigger = m_trigger, next]() -> void
        {
            Change(
                editor,
                element,
                trigger,
                [](std::vector<ButtonAction>& edited) -> void { edited.emplace_back(ExecButtonAction{}); }
            );
            editor.Text().Begin(
                std::format("action.{}", next),
                "",
                [&editor, element, trigger, next](const std::string& typed) -> void
                {
                    Change(
                        editor,
                        element,
                        trigger,
                        [next, &typed](std::vector<ButtonAction>& edited) -> void
                        {
                            if (next < edited.size())
                            {
                                edited.at(next) = ExecButtonAction{.command = SplitCommand(typed)};
                            }
                        }
                    );
                }
            );
        }
    );

    auto& type_text = first.AddComponent<ButtonComponent>("type-text", "Type text");
    type_text.SetFontSize(PanelStyle::kTextFont);
    type_text.SetFgColor(text);
    type_text.SetBgColor(surface);
    type_text.SetEllipsize(true);
    type_text.SetOnRelease(
        [&editor = m_editor, element = m_element, trigger = m_trigger, next]() -> void
        {
            Change(
                editor,
                element,
                trigger,
                [](std::vector<ButtonAction>& edited) -> void { edited.emplace_back(TypeTextButtonAction{}); }
            );
            editor.Text().Begin(
                std::format("action.{}", next),
                "",
                [&editor, element, trigger, next](const std::string& typed) -> void
                {
                    Change(
                        editor,
                        element,
                        trigger,
                        [next, &typed](std::vector<ButtonAction>& edited) -> void
                        {
                            if (next < edited.size())
                            {
                                edited.at(next) = TypeTextButtonAction{.text = typed};
                            }
                        }
                    );
                }
            );
        }
    );

    auto& second = m_stack.AddComponent<RowComponent>("keys", PanelStyle::kRowHeight, PanelStyle::kGap);

    auto& shortcut = second.AddComponent<ButtonComponent>("shortcut", "Shortcut");
    shortcut.SetFontSize(PanelStyle::kTextFont);
    shortcut.SetFgColor(text);
    shortcut.SetBgColor(surface);
    shortcut.SetEllipsize(true);
    shortcut.SetOnRelease(
        [&editor = m_editor, element = m_element, trigger = m_trigger]() -> void
        {
            editor.Text().Capture(
                true,
                [&editor, element, trigger](const std::vector<VirtualKey>& keys) -> void
                {
                    Change(
                        editor,
                        element,
                        trigger,
                        [&keys](std::vector<ButtonAction>& edited) -> void
                        { edited.emplace_back(ShortcutButtonAction{.keys = keys}); }
                    );
                }
            );
        }
    );

    auto& keyboard = second.AddComponent<ButtonComponent>("keyboard", "Keyboard");
    keyboard.SetFontSize(PanelStyle::kTextFont);
    keyboard.SetFgColor(text);
    keyboard.SetBgColor(surface);
    keyboard.SetEllipsize(true);
    keyboard.SetOnRelease(
        [&editor = m_editor, element = m_element, trigger = m_trigger]() -> void
        {
            Change(
                editor,
                element,
                trigger,
                [](std::vector<ButtonAction>& edited) -> void
                { edited.emplace_back(OverlayButtonAction{.target = std::string(kKeyboardName), .action = OverlayToggleVisible{}}); }
            );
        }
    );

    auto& third = m_stack.AddComponent<RowComponent>("view", PanelStyle::kRowHeight, PanelStyle::kGap);

    auto& recenter = third.AddComponent<ButtonComponent>("recenter", "Recenter");
    recenter.SetFontSize(PanelStyle::kTextFont);
    recenter.SetFgColor(text);
    recenter.SetBgColor(surface);
    recenter.SetEllipsize(true);
    recenter.SetOnRelease(
        [&editor = m_editor, element = m_element, trigger = m_trigger]() -> void
        {
            Change(
                editor,
                element,
                trigger,
                [](std::vector<ButtonAction>& edited) -> void
                { edited.emplace_back(SystemButtonAction{.action = SystemAction::Recenter}); }
            );
        }
    );

    auto& show_hide = third.AddComponent<ButtonComponent>("show-hide", "Show/hide");
    show_hide.SetFontSize(PanelStyle::kTextFont);
    show_hide.SetFgColor(text);
    show_hide.SetBgColor(surface);
    show_hide.SetEllipsize(true);
    show_hide.SetOnRelease(
        [&editor = m_editor, element = m_element, trigger = m_trigger]() -> void
        {
            Change(
                editor,
                element,
                trigger,
                [](std::vector<ButtonAction>& edited) -> void
                { edited.emplace_back(SystemButtonAction{.action = SystemAction::ShowHide}); }
            );
        }
    );

    auto& fourth = m_stack.AddComponent<RowComponent>("system", PanelStyle::kRowHeight, PanelStyle::kGap);

    auto& next_layout = fourth.AddComponent<ButtonComponent>("next-layout", "Next layout");
    next_layout.SetFontSize(PanelStyle::kTextFont);
    next_layout.SetFgColor(text);
    next_layout.SetBgColor(surface);
    next_layout.SetEllipsize(true);
    next_layout.SetOnRelease(
        [&editor = m_editor, element = m_element, trigger = m_trigger]() -> void
        {
            Change(
                editor,
                element,
                trigger,
                [](std::vector<ButtonAction>& edited) -> void { edited.emplace_back(KeyboardProfileButtonAction{}); }
            );
        }
    );

    auto& open_editor = fourth.AddComponent<ButtonComponent>("editor", "Editor");
    open_editor.SetFontSize(PanelStyle::kTextFont);
    open_editor.SetFgColor(text);
    open_editor.SetBgColor(surface);
    open_editor.SetEllipsize(true);
    open_editor.SetOnRelease(
        [&editor = m_editor, element = m_element, trigger = m_trigger]() -> void
        {
            Change(
                editor,
                element,
                trigger,
                [](std::vector<ButtonAction>& edited) -> void
                { edited.emplace_back(SystemButtonAction{.action = SystemAction::Edit}); }
            );
        }
    );
}
