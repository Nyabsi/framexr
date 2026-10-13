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

#include "ScreenPipeline.hpp"

#include <array>
#include <utility>

#ifndef _WIN32
#endif

#include "CommandBatch.hpp"
#include "CommandBuffer.hpp"
#include "Config.hpp"
#include "Device.hpp"
#include "Image.hpp"
#include "Interaction.hpp"
#include "Compose.hpp"
#include "Pipeline.hpp"
#include "ScreenRenderer.hpp"
#include "Shaders.hpp"
#include "framexr_config.h"

#if FRAMEXR_FEATURE_WAYLAND
#endif

#if FRAMEXR_FEATURE_X11
#endif

#ifdef _WIN32
#include "DxgiCapture.hpp"
#endif

namespace {

constexpr float kCursorSize = 16.0f / 1440.0f;

constexpr float kLowpassCutoff = 1.0f;

constexpr float kLowpassShape = 4.0f;

} // namespace

ScreenPipeline::ScreenPipeline(std::shared_ptr<Pipeline> pipeline, std::shared_ptr<Image> mouse, VkExtent2D extent)
    : m_pipeline(std::move(pipeline)),
      m_mouse(std::move(mouse)),
      m_extent(extent) {
}

auto ScreenPipeline::Create(VkExtent2D extent) -> Result<ScreenPipeline> {
    ASSIGN_OR_RETURN(auto pipeline, g_device->CreatePipeline(Shaders::Screen(), g_device->NativeFormat(), Blend::SquareSource));

    constexpr std::array<uint8_t, 64> kMouseBytes{
        0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff,
        0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff,
        0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff,
        0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff,
    };
    ASSIGN_OR_RETURN(auto upload, g_device->CreateCommandBuffer(g_device->TransferQueue()));
    ASSIGN_OR_RETURN(auto mouse, upload.UploadImage(4, 4, VK_FORMAT_R8G8B8A8_UNORM, std::as_bytes(std::span(kMouseBytes))));
    RETURN_IF_ERROR(upload.SubmitUpload());

    return ScreenPipeline(std::move(pipeline), std::move(mouse), extent);
}

auto ScreenPipeline::Render(
    const CapturedFrame& frame,
    const std::shared_ptr<Image>& target,
    CommandBatch& batch,
    float alpha,
    const TexelFootprint& footprint
) -> Result<> {
    const float filtered = g_config->screen_lowpass_filter ? 1.0f : 0.0f;
    const std::array uniform{
        footprint.size.x(),
        footprint.size.y(),
        footprint.display_pixels_per_radian.x(),
        footprint.display_pixels_per_radian.y(),
        footprint.eye.x(),
        footprint.eye.y(),
        footprint.eye.z(),
        kLowpassCutoff,
        kLowpassShape,
        alpha,
        filtered,
    };
    const std::array cursor_uniform{1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, kLowpassCutoff, kLowpassShape, alpha, 0.0f};

    ASSIGN_OR_RETURN(auto texture_set, m_pipeline->CreateTextureSet(0, frame.Texture(), g_device->TextureFilter()));
    ASSIGN_OR_RETURN(auto alpha_set, m_pipeline->CreateUniformSet(1, uniform));
    const auto pass = m_pipeline->CreatePassForTarget(*target, {std::move(texture_set), std::move(alpha_set)});

    ASSIGN_OR_RETURN(auto command_buffer, g_device->CreateCommandBuffer(g_device->GraphicsQueue()));
    command_buffer.BeginRendering(target);
    command_buffer.Draw(pass);

    if (frame.Mouse()) {
        const auto width = static_cast<float>(m_extent.width);
        const auto height = static_cast<float>(m_extent.height);
        const float size = kCursorSize * height;
        const float half_size = size * 0.5f;

        ASSIGN_OR_RETURN(
            auto vertices,
            g_device->UploadVertices(
                m_extent,
                std::fma(frame.Mouse()->x, width, -half_size),
                std::fma(frame.Mouse()->y, height, -half_size),
                size,
                size
            )
        );
        ASSIGN_OR_RETURN(auto mouse_set, m_pipeline->CreateTextureSet(0, m_mouse, VK_FILTER_NEAREST));
        ASSIGN_OR_RETURN(auto mouse_alpha_set, m_pipeline->CreateUniformSet(1, cursor_uniform));
        command_buffer.Draw(
            m_pipeline->CreatePass(target->Extent(), std::move(vertices), {std::move(mouse_set), std::move(mouse_alpha_set)})
        );
    }

    command_buffer.EndRendering();
    batch.Push(std::move(command_buffer));
    return {};
}
