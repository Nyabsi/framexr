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

#include "Control.hpp"
#include "KeyboardState.hpp"
#include "Pointer.hpp"
#include "VirtualKey.hpp"

class KeyControl final : public Control {
  public:
    KeyControl(std::shared_ptr<KeyboardState> state, VirtualKey key);

    auto OnPress(PointerMode mode) -> void override;
    auto OnRelease() -> void override;
    auto HighlightColor() -> std::optional<Eigen::Vector4f> override;
    auto Outline() -> std::optional<HighlightOutline> override;

    [[nodiscard]] auto Latched() const -> bool { return m_latched; }

  private:
    std::shared_ptr<KeyboardState> m_state;
    VirtualKey m_key;
    VirtualKey m_sent;
    bool m_latches;
    bool m_latched = false;
    bool m_translated = false;
    bool m_captured = false;
};
