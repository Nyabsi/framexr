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

#include "ScopedRecordingHid.hpp"

#include "HidProvider.hpp"
#include "RecordingHid.hpp"

ScopedRecordingHid::ScopedRecordingHid() : m_recording(std::make_unique<RecordingHid>()), m_previous(g_hid)
{
    g_hid = m_recording.get();
}

ScopedRecordingHid::~ScopedRecordingHid()
{
    g_hid = m_previous;
}
