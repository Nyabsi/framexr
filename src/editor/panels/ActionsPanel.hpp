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

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Core>

#include "FormComponent.hpp"
#include "document/Actions.hpp"

class Editor;

class StackComponent;

enum class ButtonTrigger : uint8_t;

using ActionsChange = std::function<void(std::vector<ButtonAction>&)>;

struct TriggerSlot
{
    ButtonTrigger trigger;
    std::string_view name;
    std::optional<std::vector<ButtonAction>> ButtonData::* actions;
};

class ActionsPanel final : public FormComponent
{
  public:
    static auto Triggers() -> std::span<const TriggerSlot>;
    static auto Describe(const ButtonAction& action) -> std::string;
    static auto TriggerName(ButtonTrigger trigger) -> std::string_view;
    static auto ActionsOf(ButtonData& data, ButtonTrigger trigger) -> std::optional<std::vector<ButtonAction>>&;
    static auto SplitCommand(std::string_view text) -> std::vector<std::string>;
    static auto JoinCommand(const std::vector<std::string>& command) -> std::string;

    ActionsPanel(Editor& editor, const Eigen::Vector2f& size);

  private:
    static auto Change(Editor& editor, size_t element, ButtonTrigger trigger, const ActionsChange& apply) -> void;
    auto AddAction(const ButtonAction& action, size_t position) -> void;
    auto AddChoices() -> void;

    Editor& m_editor;
    size_t m_element = 0;
    ButtonTrigger m_trigger;
    StackComponent& m_stack;
};
