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

#include "OpenXrLines.hpp"

#include <array>
#include <utility>

#include "CommandBatch.hpp"
#include "Device.hpp"
#include "Logger.hpp"
#include "Runtime.hpp"
#include "OpenXrPose.hpp"
#include "Shaders.hpp"

namespace {

constexpr float kLineWidth = 0.002f;

constexpr auto kColors = std::to_array<std::array<float, 6>>({
    {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.375f, 0.5f, 1.0f, 0.0f, 0.0f},
    {0.69f, 0.188f, 0.0f, 1.0f, 0.0f, 0.0f},
    {0.375f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f},
    {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
});

} // namespace

auto OpenXrLines::Create() -> Result<std::unique_ptr<OpenXrLines>> {
    ASSIGN_OR_RETURN(auto pipeline, g_device->CreatePipeline(Shaders::Line(), g_device->NativeFormat(), Blend::Disabled));
    return std::unique_ptr<OpenXrLines>(new OpenXrLines(std::move(pipeline)));
}

OpenXrLines::OpenXrLines(std::shared_ptr<Pipeline> pipeline) : m_pipeline(std::move(pipeline)) {
}

auto OpenXrLines::Allocate(const OpenXrState& xr) -> Result<size_t> {
    const auto id = m_next_id++;
    ASSIGN_OR_RETURN(auto swapchain, OpenXrSwapchain::Create(*xr.instance, xr.session, {.width = 1, .height = 1}, false));
    m_lines.emplace(id, OpenXrLineContainer{.swapchain = std::move(swapchain)});
    return id;
}

auto OpenXrLines::DrawFrom(size_t id, Eigen::Affine3f from, float length, size_t color, const Eigen::Affine3f& hmd) -> void {
    if (length < 0.01f) {
        return;
    }
    const auto entry = m_lines.find(id);
    if (entry == m_lines.end()) {
        LOG_WARN("Line {} not found", id);
        return;
    }

    const Eigen::Affine3f transform = Runtime::BeamPose(from, length, hmd);
    entry->second.line = OpenXrLine{.color = color, .pose = OpenXrPose::FromTransform(transform), .length = length};
}

auto OpenXrLines::Render(CommandBatch& batch) -> Result<> {
    for (auto& [id, container] : m_lines) {
        if (!container.line) {
            continue;
        }
        ASSIGN_OR_RETURN(auto target, container.swapchain->AcquireWaitImage());
        ASSIGN_OR_RETURN(auto color_set, m_pipeline->CreateUniformSet(0, kColors.at(container.line->color)));
        const auto pass = m_pipeline->CreatePassForTarget(*target, {std::move(color_set)});

        ASSIGN_OR_RETURN(auto command_buffer, g_device->CreateCommandBuffer(g_device->GraphicsQueue()));
        command_buffer.BeginRendering(target);
        command_buffer.Draw(pass);
        command_buffer.EndRendering();
        batch.Push(std::move(command_buffer));
    }
    return {};
}

auto OpenXrLines::Present(const OpenXrState& xr) -> Result<std::vector<OpenXrLayer>> {
    std::vector<OpenXrLayer> layers;
    for (auto& [id, container] : m_lines) {
        RETURN_IF_ERROR(container.swapchain->EnsureImageReleased());
        if (const auto line = std::exchange(container.line, std::nullopt)) {
            layers.emplace_back(
                XrCompositionLayerQuad{
                    .type = XR_TYPE_COMPOSITION_LAYER_QUAD,
                    .space = xr.stage,
                    .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
                    .subImage = container.swapchain->SubImage(),
                    .pose = line->pose,
                    .size = {.width = kLineWidth, .height = line->length},
                }
            );
        }
    }
    return layers;
}
