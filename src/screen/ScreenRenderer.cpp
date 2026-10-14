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

#include "ScreenRenderer.hpp"

#include <algorithm>
#include <format>
#include <numbers>
#include <utility>

#include "CommandBatch.hpp"
#include "CommandBuffer.hpp"
#include "Config.hpp"
#include "Device.hpp"
#include "Image.hpp"
#include "Interaction.hpp"
#include "Logger.hpp"
#include "Compose.hpp"
#include "PendingFrame.hpp"
#include "Pipeline.hpp"
#include "framexr_config.h"

#ifdef _WIN32
#include "DxgiCapture.hpp"
#endif

namespace
{

constexpr uint32_t kMaxScreenHeight = 2560;

auto ExtentFromFormat(const FrameFormat& format) -> VkExtent2D
{
    const uint32_t height_limit = g_config->screen_render_down ? std::min<uint32_t>(g_config->screen_max_height, kMaxScreenHeight)
                                                               : kMaxScreenHeight;
    const uint32_t height = std::min(format.height, height_limit);
    const auto width = static_cast<uint32_t>(
        static_cast<float>(format.width) / static_cast<float>(format.height) * static_cast<float>(height)
    );
    return {width, height};
}

auto AffineFromFormat(const FrameFormat& format) -> Eigen::Affine3f
{
    const auto rotation = [](float angle) -> Eigen::Affine3f
    { return Eigen::Affine3f(Eigen::AngleAxisf(angle, Eigen::Vector3f::UnitZ())); };
    Eigen::Affine3f flip_x(Eigen::Scaling(-1.0f, 1.0f, 1.0f));
    constexpr float kPi = std::numbers::pi_v<float>;

    switch (format.transform)
    {
    case FrameTransform::Rotated90: return rotation(-kPi / 2.0f);
    case FrameTransform::Rotated180: return rotation(kPi);
    case FrameTransform::Rotated270: return rotation(kPi / 2.0f);
    case FrameTransform::Flipped: return flip_x;
    case FrameTransform::Flipped90: return flip_x * rotation(-kPi / 2.0f);
    case FrameTransform::Flipped180: return flip_x * rotation(kPi);
    case FrameTransform::Flipped270: return flip_x * rotation(kPi / 2.0f);
    case FrameTransform::Undefined:
    case FrameTransform::Normal: break;
    }
    return Eigen::Affine3f::Identity();
}

auto AllowsDmabuf(const std::string& method) -> bool
{
    return method != "pw_fallback" && method != "screencopy";
}

} // namespace

ScreenRenderer::ScreenRenderer(std::string name, std::unique_ptr<Capture> capture)
    : m_name(std::move(name)),
      m_capture(std::move(capture))
{
}

auto ScreenRenderer::Start() -> void
{
    const bool supports_dmabuf = g_device->SupportsDmabuf() && m_capture->SupportsDmabuf();
    const auto& capture_method = g_config->capture_method;
    const bool allow_dmabuf = AllowsDmabuf(capture_method);

    std::span<const DrmFormat> dmabuf_formats;
    if (!supports_dmabuf || !allow_dmabuf)
    {
        if (!supports_dmabuf)
        {
            LOG_INFO("Capture method does not support DMA-buf");
        }
        else
        {
            LOG_INFO("Not using DMA-buf capture due to {}", capture_method);
        }
        if (g_device->CaptureQueue() == nullptr)
        {
            LOG_WARN(
                "Current GPU does not support multiple queues. Software capture will take place on the main thread. Expect "
                "degraded performance."
            );
        }
    }
    else
    {
        LOG_WARN("Using DMA-buf capture. If screens are blank for you, switch to SHM using:");
        LOG_WARN("echo 'capture_method: pw_fallback' > ~/.config/framexr/conf.d/pw_fallback.yaml");
        dmabuf_formats = g_device->DrmFormats();
    }

    auto* capture_queue = g_device->CaptureQueue();
    m_main_thread = capture_queue == nullptr;
    m_uploader = std::make_shared<FrameUploader>(m_name, m_main_thread ? g_device->TransferQueue() : *capture_queue);

    if (m_main_thread)
    {
        m_capture->Start(
            dmabuf_formats,
            [](Frame&& frame) -> std::unique_ptr<ProcessedFrame> { return std::make_unique<PendingFrame>(std::move(frame)); }
        );
    }
    else
    {
        m_capture->Start(
            dmabuf_formats,
            [uploader = m_uploader](Frame&& frame) -> std::unique_ptr<ProcessedFrame>
            { return uploader->Process(std::move(frame)); }
        );
    }
    m_capture->RequestNewFrame();
}

auto ScreenRenderer::ShouldRender() -> Result<RenderReadiness>
{
    if (!m_capture->IsStarted())
    {
        Start();
        return RenderReadiness::Unable;
    }

    if (auto received = m_capture->Receive())
    {
        if (m_main_thread)
        {
            auto& pending = static_cast<PendingFrame&>(*received);
            if (auto processed = m_uploader->Process(std::move(pending.Source())))
            {
                m_current = std::move(processed);
            }
        }
        else
        {
            m_current.reset(static_cast<CapturedFrame*>(received.release()));
        }
    }

    if (m_current && !m_meta)
    {
        m_meta = FrameMeta{
            .extent = ExtentFromFormat(m_current->Format()),
            .transform = AffineFromFormat(m_current->Format()),
            .format = m_current->Texture()->Format(),
        };
        ASSIGN_OR_RETURN(auto pipeline, ScreenPipeline::Create(m_current->Texture()->Extent()));
        m_pipeline.emplace(std::move(pipeline));
    }

    return m_current ? RenderReadiness::Should : RenderReadiness::Unable;
}

auto ScreenRenderer::Render(const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool>
{
    if (!m_current)
    {
        return false;
    }
    const auto frame = std::move(m_current);
    if (!m_pipeline)
    {
        FatalError("Screen pipeline missing while a frame is pending");
    }
    RETURN_IF_ERROR(m_pipeline->Render(*frame, target, batch, alpha, m_footprint));
    m_capture->RequestNewFrame();
    return true;
}

auto ScreenRenderer::Pause() -> Result<>
{
    m_capture->Pause();
    return {};
}

auto ScreenRenderer::Resume() -> Result<>
{
    m_capture->Resume();
    return {};
}

auto ScreenRenderer::Meta() -> std::optional<FrameMeta>
{
    return m_meta;
}

auto ScreenRenderer::SetTexelFootprint(const TexelFootprint& footprint) -> void
{
    m_footprint = footprint;
}
