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

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Error.hpp"
#include "ImeScript.hpp"
#include "VirtualKeys.hpp"

class ImeContext
{
  public:
    static auto Connect(ImeScript script) -> Result<std::unique_ptr<ImeContext>>;

    virtual ~ImeContext() = default;
    ImeContext(const ImeContext&) = delete;
    auto operator=(const ImeContext&) -> ImeContext& = delete;

    [[nodiscard]] auto Preedit() const -> const std::string& { return m_preedit; }
    [[nodiscard]] auto Candidates() const -> const std::vector<std::string>& { return m_candidates; }

    auto SetOnCommit(std::function<void(const std::string&)> on_commit) -> void { m_on_commit = std::move(on_commit); }
    auto SetOnForward(std::function<void(uint32_t)> on_forward) -> void { m_on_forward = std::move(on_forward); }
    auto SetOnChange(std::function<void()> on_change) -> void { m_on_change = std::move(on_change); }

    virtual auto Key(VirtualKey key, uint32_t keysym, uint8_t modifiers) -> bool = 0;
    virtual auto Poll() -> void = 0;

  protected:
    ImeContext() = default;

    std::string m_preedit;
    std::vector<std::string> m_candidates;
    std::function<void(const std::string&)> m_on_commit;
    std::function<void(uint32_t)> m_on_forward;
    std::function<void()> m_on_change;
};
