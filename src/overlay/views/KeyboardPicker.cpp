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

#include "views/KeyboardPicker.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ButtonComponent.hpp"
#include "ChoiceGridComponent.hpp"
#include "Compose.hpp"
#include "Config.hpp"
#include "Container.hpp"
#include "Form.hpp"
#include "KeyboardLayout.hpp"
#include "StackComponent.hpp"
#include "TaskQueue.hpp"
#include "TextLineComponent.hpp"
#include "views/Keyboard.hpp"

namespace
{

constexpr float kWidth = 380.0f;
constexpr float kPadding = 12.0f;
constexpr float kGap = 6.0f;
constexpr float kRowHeight = 30.0f;
constexpr float kButtonHeight = 34.0f;
constexpr float kTitleHeight = 24.0f;
constexpr int kTitleFont = 18;
constexpr int kTextFont = 14;
constexpr float kWidthMeters = 0.35f;
constexpr float kInFront = 0.05f;

} // namespace

auto KeyboardPicker::Open(std::shared_ptr<const Keymap> keymap) -> void
{
    g_tasks->Enqueue(DropOverlayTask{.selector = std::string(kKeyboardPickerName)});
    g_tasks->Enqueue(
        CreateOverlayTask{
            .selector = std::string(kKeyboardPickerName),
            .create = [keymap = std::move(keymap)]() -> std::unique_ptr<View> { return Create(keymap); },
        }
    );
}

auto KeyboardPicker::Create(const std::shared_ptr<const Keymap>& keymap) -> std::unique_ptr<KeyboardPicker>
{
    auto* keyboard = g_container->ByName(kKeyboardName);
    if (keyboard == nullptr)
    {
        return nullptr;
    }

    auto form = std::make_unique<Form>(std::string(kKeyboardPickerName));
    auto& stack = form->AddComponent<StackComponent>("stack", kPadding, kGap);
    stack.SetBgColor(FormTheme::Rgba("@panel"));

    stack.AddComponent<TextLineComponent>("title", "Choose a keyboard", kTitleFont, FormTheme::Rgba("@text"), kTitleHeight);

    std::vector<std::string> names;
    std::optional<size_t> current;
    for (const auto& profile : KeyboardLayout::All())
    {
        if (profile.Name() == KeyboardLayout::Find(g_config->keyboard_profile).Name())
        {
            current = names.size();
        }
        names.push_back(profile.Name());
    }
    auto& choices = stack.AddComponent<ChoiceGridComponent>("keyboards", names, current, 2, kRowHeight);
    choices.SetOnChoose(
        [keymap, names](size_t index) -> void
        {
            Keyboard::SwitchProfile(keymap, names.at(index));
            g_tasks->Enqueue(DropOverlayTask{.selector = std::string(kKeyboardPickerName)});
        }
    );

    auto& cancel = stack.AddComponent<ButtonComponent>("cancel", "Cancel");
    cancel.SetSize({0.0f, kButtonHeight});
    cancel.SetFontSize(kTextFont);
    cancel.SetFgColor(FormTheme::Rgba("@text"));
    cancel.SetBgColor(FormTheme::Rgba("@surface"));
    cancel.SetEllipsize(true);
    cancel.SetOnRelease([]() -> void { g_tasks->Enqueue(DropOverlayTask{.selector = std::string(kKeyboardPickerName)}); });

    const Eigen::Vector2f size(kWidth, stack.Bottom() - kGap + kPadding);
    form->SetSize(size);
    stack.SetSize(size);

    auto created = Compose::Create(std::move(form));
    if (!created)
    {
        return nullptr;
    }

    const Eigen::Matrix3f linear = keyboard->Transform().linear();
    const Eigen::Matrix3f facing = linear / linear.col(0).norm();
    Eigen::Affine3f pose = Eigen::Affine3f::Identity();
    pose.translation() = keyboard->Transform().translation() + facing * Eigen::Vector3f(0.0f, 0.0f, kInFront);
    pose.linear() = facing * kWidthMeters;

    auto picker = std::unique_ptr<KeyboardPicker>(new KeyboardPicker());
    picker->SetComposer(std::move(*created));
    picker->SetName(std::string(kKeyboardPickerName));
    picker->SetWantVisible(true);
    picker->SetInteractable(true);
    picker->SetAttachedTo(RelativeTo::Stage);
    picker->SetZOrder(kZOrderToast);
    picker->SetTransform(pose);
    picker->SetInteractionTransform(UiTransform({static_cast<uint32_t>(size.x()), static_cast<uint32_t>(size.y())}));
    return picker;
}

auto KeyboardPicker::Update() -> void
{
    if (const auto* keyboard = g_container->ByName(kKeyboardName); keyboard == nullptr || !keyboard->WantVisible())
    {
        g_tasks->Enqueue(DropOverlayTask{.selector = std::string(kKeyboardPickerName)});
    }
}
