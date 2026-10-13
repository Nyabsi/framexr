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

#include <memory>

#include <gtest/gtest.h>

class Logger;
class FormTheme;

class UnitEnvironment final : public testing::Environment {
  public:
    UnitEnvironment();
    ~UnitEnvironment() override;
    UnitEnvironment(const UnitEnvironment&) = delete;
    auto operator=(const UnitEnvironment&) -> UnitEnvironment& = delete;

    auto SetUp() -> void override;
    auto TearDown() -> void override;

  private:
    std::unique_ptr<Logger> m_logger;
    std::unique_ptr<FormTheme> m_themes;
};
