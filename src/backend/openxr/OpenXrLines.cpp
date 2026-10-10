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
#include <numbers>
#include <utility>

#include "Device.hpp"
#include "Logger.hpp"
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

auto AxisAngle(const Eigen::Vector3f& axis, float angle) -> Eigen::Affine3f {
    Eigen::Affine3f transform = Eigen::Affine3f::Identity();
    transform.linear() = Eigen::AngleAxisf(angle, axis).toRotationMatrix();
    return transform;
}

} // namespace

auto OpenXrLines::Create(Device& device) -> Result<std::unique_ptr<OpenXrLines>> {
    ASSIGN_OR_RETURN(auto pipeline, device.CreatePipeline(Shaders::Line(), device.NativeFormat(), Blend::Disabled));
    return std::unique_ptr<OpenXrLines>(new OpenXrLines(std::move(pipeline)));
}

OpenXrLines::OpenXrLines(std::shared_ptr<Pipeline> pipeline) : pipeline_(std::move(pipeline)) {
}

auto OpenXrLines::Allocate(const OpenXrState& xr, Device& device) -> Result<size_t> {
    const auto id = next_id_++;
    ASSIGN_OR_RETURN(auto swapchain, OpenXrSwapchain::Create(*xr.instance, xr.session, device, {.width = 1, .height = 1}, false));
    lines_.emplace(id, OpenXrLineContainer{.swapchain = std::move(swapchain)});
    return id;
}

auto OpenXrLines::DrawFrom(size_t id, Eigen::Affine3f from, float length, size_t color, const Eigen::Affine3f& hmd) -> void {
    if (length < 0.01f) {
        return;
    }
    const auto entry = lines_.find(id);
    if (entry == lines_.end()) {
        LOG_WARN("Line {} not found", id);
        return;
    }

    constexpr float kPi = std::numbers::pi_v<float>;
    from.translation() += from.linear() * (-Eigen::Vector3f::UnitZ()) * (length * 0.5f);
    Eigen::Affine3f transform = from * AxisAngle(Eigen::Vector3f::UnitX(), kPi * 1.5f);

    const Eigen::Vector3f to_hmd = hmd.translation() - from.translation();
    const std::array<Eigen::Vector3f, 4> sides{
        Eigen::Vector3f::UnitZ(),
        Eigen::Vector3f::UnitX(),
        -Eigen::Vector3f::UnitZ(),
        -Eigen::Vector3f::UnitX(),
    };
    const std::array<Eigen::Affine3f, 4> rotations{
        Eigen::Affine3f::Identity(),
        AxisAngle(Eigen::Vector3f::UnitY(), kPi * 0.5f),
        AxisAngle(Eigen::Vector3f::UnitY(), -kPi),
        AxisAngle(Eigen::Vector3f::UnitY(), kPi * 1.5f),
    };

    size_t closest = 0;
    float closest_dot = 0.0f;
    const Eigen::Matrix3f linear = transform.linear();
    for (size_t i = 0; i < sides.size(); ++i) {
        const float dot = to_hmd.dot(linear * sides[i]);
        if (i == 0 || dot > closest_dot) {
            closest = i;
            closest_dot = dot;
        }
    }
    transform = transform * rotations[closest];

    entry->second.line = OpenXrLine{.color = color, .pose = OpenXrPose::FromTransform(transform), .length = length};
}

auto OpenXrLines::Render(Device& device, CommandBatch& batch) -> Result<> {
    for (auto& [id, container] : lines_) {
        if (!container.line) {
            continue;
        }
        ASSIGN_OR_RETURN(auto target, container.swapchain->AcquireWaitImage());
        ASSIGN_OR_RETURN(auto color_set, pipeline_->CreateUniformSet(0, kColors.at(container.line->color)));
        const auto pass = pipeline_->CreatePassForTarget(*target, {std::move(color_set)});

        ASSIGN_OR_RETURN(auto command_buffer, device.CreateCommandBuffer(device.GraphicsQueue()));
        command_buffer.BeginRendering(target);
        command_buffer.Draw(pass);
        command_buffer.EndRendering();
        batch.Push(std::move(command_buffer));
    }
    return {};
}

auto OpenXrLines::Present(const OpenXrState& xr) -> Result<std::vector<OpenXrLayer>> {
    std::vector<OpenXrLayer> layers;
    for (auto& [id, container] : lines_) {
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
