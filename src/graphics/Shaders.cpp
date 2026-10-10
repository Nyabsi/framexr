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

#include "Shaders.hpp"

namespace {

alignas(4) constexpr unsigned char kCommonVertex[] = {
#embed "CommonVertex.spv"
};
alignas(4) constexpr unsigned char kColorFragment[] = {
#embed "ColorFragment.spv"
};
alignas(4) constexpr unsigned char kLineFragment[] = {
#embed "LineFragment.spv"
};
alignas(4) constexpr unsigned char kGlyphFragment[] = {
#embed "GlyphFragment.spv"
};
alignas(4) constexpr unsigned char kSpriteFragment[] = {
#embed "SpriteFragment.spv"
};
alignas(4) constexpr unsigned char kSpriteTintFragment[] = {
#embed "SpriteTintFragment.spv"
};
alignas(4) constexpr unsigned char kSpriteHighlightFragment[] = {
#embed "SpriteHighlightFragment.spv"
};
alignas(4) constexpr unsigned char kGridFragment[] = {
#embed "GridFragment.spv"
};
alignas(4) constexpr unsigned char kScreenFragment[] = {
#embed "ScreenFragment.spv"
};
alignas(4) constexpr unsigned char kSrgbFragment[] = {
#embed "SrgbFragment.spv"
};
alignas(4) constexpr unsigned char kSwapchainFragment[] = {
#embed "SwapchainFragment.spv"
};

} // namespace

auto Shaders::Color() -> ShaderProgram {
    return {kCommonVertex, kColorFragment, {DescriptorKind::Uniform}};
}

auto Shaders::Line() -> ShaderProgram {
    return {kCommonVertex, kLineFragment, {DescriptorKind::Uniform}};
}

auto Shaders::Glyph() -> ShaderProgram {
    return {kCommonVertex, kGlyphFragment, {DescriptorKind::Texture, DescriptorKind::Uniform}};
}

auto Shaders::Sprite() -> ShaderProgram {
    return {kCommonVertex, kSpriteFragment, {DescriptorKind::Texture}};
}

auto Shaders::SpriteTint() -> ShaderProgram {
    return {kCommonVertex, kSpriteTintFragment, {DescriptorKind::Texture, DescriptorKind::Uniform}};
}

auto Shaders::SpriteHighlight() -> ShaderProgram {
    return {kCommonVertex, kSpriteHighlightFragment, {DescriptorKind::Texture, DescriptorKind::Uniform}};
}

auto Shaders::Grid() -> ShaderProgram {
    return {kCommonVertex, kGridFragment, {}};
}

auto Shaders::Screen() -> ShaderProgram {
    return {kCommonVertex, kScreenFragment, {DescriptorKind::Texture, DescriptorKind::Uniform}};
}

auto Shaders::Srgb() -> ShaderProgram {
    return {kCommonVertex, kSrgbFragment, {DescriptorKind::Texture, DescriptorKind::Uniform}};
}

auto Shaders::Swapchain() -> ShaderProgram {
    return {kCommonVertex, kSwapchainFragment, {DescriptorKind::Texture, DescriptorKind::Uniform}};
}
