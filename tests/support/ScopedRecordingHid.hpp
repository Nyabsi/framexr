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
#include <memory>
#include <vector>

#include "HidProvider.hpp"
#include "RecordingHid.hpp"

class ScopedRecordingHid final
{
  public:
    ScopedRecordingHid();
    ~ScopedRecordingHid();
    ScopedRecordingHid(const ScopedRecordingHid&) = delete;
    auto operator=(const ScopedRecordingHid&) -> ScopedRecordingHid& = delete;

    [[nodiscard]] auto Events() const -> const std::vector<HidEvent>& { return m_recording->Events(); }
    [[nodiscard]] auto Buttons() const -> const std::vector<ButtonEvent>& { return m_recording->Buttons(); }
    [[nodiscard]] auto Wheels() const -> size_t { return m_recording->Wheels(); }
    [[nodiscard]] auto InputMethods() const -> const std::vector<ImeScript>& { return m_recording->InputMethods(); }
    auto Clear() -> void { m_recording->Clear(); }

  private:
    std::unique_ptr<RecordingHid> m_recording;
    HidProvider* m_previous;
};
