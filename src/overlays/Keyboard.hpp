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
#include "CommandBatch.hpp"
#include "Control.hpp"
#include "Error.hpp"
#include "InteractionHandler.hpp"
#include "KeyboardLayout.hpp"
#include "KeyboardState.hpp"
#include "Keymap.hpp"
#include "KeyTranslator.hpp"
#include "Overlay.hpp"
#include "OverlayRenderer.hpp"
#include "VirtualKey.hpp"

constexpr std::string_view kKeyboardName = "kbd";

struct KeyboardPlacement {
    float scale = 1.0f;
    Eigen::Vector2f origin = Eigen::Vector2f::Zero();
    bool interactive = true;
};

class Keyboard final {
  public:
    static constexpr float kPixelsPerUnit = 80.0f;
    static constexpr float kKeyboardMargin = 14.0f;

    static auto Create(const std::shared_ptr<const Keymap>& keymap) -> Result<std::unique_ptr<Overlay>>;
    static auto CreateFrom(const std::shared_ptr<const Keymap>& keymap, const KeyboardLayout& layout)
        -> Result<std::unique_ptr<Overlay>>;
    static auto CanvasSize(const KeyboardLayout& layout) -> Eigen::Vector2f;
    static auto KeyRect(const KeyDefinition& key, const KeyboardPlacement& placement) -> CanvasRect;
    static auto Fill(
        Canvas& canvas,
        const KeyboardLayout& layout,
        const std::shared_ptr<const Keymap>& keymap,
        const std::shared_ptr<KeyboardState>& state,
        const KeyboardPlacement& placement
    ) -> void;
    static auto SwitchProfile(std::shared_ptr<const Keymap> keymap, std::string profile) -> void;
    static auto IsLatchingKey(VirtualKey key) -> bool;
    static auto SendKey(VirtualKey key, bool down) -> void;
    static auto SetModifiers(uint8_t modifiers) -> void;
    static auto Click() -> void;
};
