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
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Error.hpp"
#include "ImeContext.hpp"

struct TsfKey
{
    VirtualKey key = VirtualKey::Escape;
    uint32_t keysym = 0;
    uint8_t modifiers = 0;
};

struct TsfOutput
{
    std::vector<std::string> commits;
    std::vector<uint32_t> forwards;
    std::string preedit;
    std::vector<std::string> candidates;
    bool changed = false;
};

class TsfContext final : public ImeContext
{
  public:
    static auto Create(ImeScript script) -> Result<std::unique_ptr<TsfContext>>;

    ~TsfContext() override;
    TsfContext(const TsfContext&) = delete;
    auto operator=(const TsfContext&) -> TsfContext& = delete;

    auto Key(VirtualKey key, uint32_t keysym, uint8_t modifiers) -> bool override;
    auto Poll() -> void override;

  private:
    explicit TsfContext(ImeScript script);

    auto Run(std::promise<Result<>> started) -> void;

    ImeScript m_script;
    std::mutex m_mutex;
    std::deque<TsfKey> m_keys;
    TsfOutput m_output;
    unsigned long m_thread_id = 0;
    std::thread m_worker;
};
