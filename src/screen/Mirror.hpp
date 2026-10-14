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

#include <future>
#include <memory>
#include <optional>
#include <string>

#include <vulkan/vulkan.h>

#include "Error.hpp"
#include "ScreenCastPortal.hpp"
#include "View.hpp"

class Mirror final : public View {
  public:
    Mirror(std::string name, bool show_hide);

    auto Update() -> void override;

  private:
    auto Attach() -> void;

    std::optional<std::future<Result<ScreenSelection>>> m_selection;
    VkExtent2D m_extent{};
};
