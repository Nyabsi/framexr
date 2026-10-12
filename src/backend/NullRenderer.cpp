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

#include "NullRenderer.hpp"

#include "CommandBatch.hpp"
#include "CommandBuffer.hpp"
#include "Image.hpp"
#include "OverlayRenderer.hpp"

auto NullRenderer::Init() -> Result<> {
    return {};
}

auto NullRenderer::Pause() -> Result<> {
    return {};
}

auto NullRenderer::Resume() -> Result<> {
    return {};
}

auto NullRenderer::ShouldRender() -> Result<RenderReadiness> {
    return RenderReadiness::Unable;
}

auto NullRenderer::Render(const std::shared_ptr<Image>&, CommandBatch&, float) -> Result<bool> {
    return false;
}

auto NullRenderer::Meta() -> std::optional<FrameMeta> {
    return std::nullopt;
}
