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

#include "Chatbox.hpp"
#include "Error.hpp"
#include "FormComponent.hpp"
#include "KeyComponent.hpp"
#include "KeyboardLayout.hpp"
#include "LabelComponent.hpp"
#include "methods/KeyboardInput.hpp"
#include "Keymap.hpp"
#include "KeyTranslator.hpp"
#include "View.hpp"
#include "VirtualKeys.hpp"

constexpr std::string_view kKeyboardName = "kbd";

struct KeyboardPlacement
{
    float scale = 1.0f;
    Eigen::Vector2f origin = Eigen::Vector2f::Zero();
};

struct LetterLegend
{
    LabelComponent* label = nullptr;
    std::string lower;
    std::string upper;
};

class Keyboard final : public View
{
  public:
    static constexpr float kPixelsPerUnit = 80.0f;
    static constexpr float kKeyboardMargin = 14.0f;

    static auto Create(const std::shared_ptr<const Keymap>& keymap) -> Result<std::unique_ptr<Keyboard>>;
    static auto CanvasSize(const KeyboardLayout& layout) -> Eigen::Vector2f;
    static auto KeyRect(const KeyDefinition& key, const KeyboardPlacement& placement) -> CanvasRect;
    static auto Fill(
        FormComponent& parent,
        const KeyboardLayout& layout,
        const std::shared_ptr<const Keymap>& keymap,
        const KeyboardPlacement& placement,
        Keyboard* owner
    ) -> void;
    static auto SwitchProfile(std::shared_ptr<const Keymap> keymap, std::string profile) -> void;

    [[nodiscard]] auto Input() -> KeyboardInput& { return m_input; }

    auto Apply(const std::shared_ptr<const Keymap>& keymap, const KeyboardLayout& layout) -> Result<>;
    auto Update() -> void override;

  private:
    Keyboard();

    auto Bind(KeyComponent& key, const KeyDefinition& definition, const std::shared_ptr<const Keymap>& keymap) -> void;

    KeyboardInput m_input;
    std::vector<LetterLegend> m_letters;
    std::vector<KeyComponent*> m_keys;
    std::shared_ptr<Chatbox> m_chatbox = std::make_shared<Chatbox>();
    KeyEmphasis m_emphasis;
    bool m_native = false;
    bool m_uppercase = false;
    ImeScript m_ime = ImeScript::None;
    ImeScript m_selected_ime = ImeScript::None;
};
