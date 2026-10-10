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

#include <filesystem>
#include <memory>

#include <gtest/gtest.h>

class AppState;

class TestApp final : public testing::Environment {
  public:
    static auto Get() -> AppState&;
    static auto OutputDir() -> const std::filesystem::path&;

    auto SetUp() -> void override;
    auto TearDown() -> void override;

  private:
    static std::unique_ptr<AppState> app_;
    static std::filesystem::path output_dir_;
};
