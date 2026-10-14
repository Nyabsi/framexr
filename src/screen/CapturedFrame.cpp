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

#include "CapturedFrame.hpp"

#include <utility>

#ifndef _WIN32
#endif

#include "Image.hpp"
#include "ScreenRenderer.hpp"
#include "framexr_config.h"

#if FRAMEXR_FEATURE_WAYLAND
#endif

#if FRAMEXR_FEATURE_X11
#endif

#ifdef _WIN32
#include "DxgiCapture.hpp"
#endif

CapturedFrame::CapturedFrame(std::shared_ptr<Image> captured_image, FrameFormat frame_format, std::optional<MouseMeta> mouse_meta)
    : m_texture(std::move(captured_image)),
      m_format(frame_format),
      m_mouse(mouse_meta) {
}
