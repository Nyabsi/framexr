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

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Keymap.hpp"
#include "VirtualKey.hpp"

struct KeyboardRegion {
    std::string layout;
    std::string variant;

    static auto Parse(std::string_view text) -> KeyboardRegion;
};

class KeyTranslator final {
  public:
    static auto Create(std::shared_ptr<const Keymap> region, std::shared_ptr<const Keymap> desktop) -> KeyTranslator;

    [[nodiscard]] auto Translate(VirtualKey key, uint8_t modifiers, bool caps_lock) const -> std::optional<TranslatedKey>;

  private:
    KeyTranslator(std::shared_ptr<const Keymap> region, std::shared_ptr<const Keymap> desktop);

    std::shared_ptr<const Keymap> region_;
    std::shared_ptr<const Keymap> desktop_;
    std::array<std::unordered_map<uint32_t, TranslatedKey>, 2> desktop_keys_;
};
