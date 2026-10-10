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

#include "OpenXrSkybox.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <optional>
#include <span>
#include <vector>

#include "Config.hpp"
#include "Device.hpp"
#include "Logger.hpp"
#include "OpenXrInstance.hpp"
#include "OpenXrPose.hpp"
#include "Shaders.hpp"
#include "State.hpp"

namespace {

constexpr unsigned char kDefaultSkybox[] = {
#embed "table_mountain_2.dds"
};

auto ReadBinaryFile(const std::filesystem::path& path) -> std::optional<std::vector<std::byte>> {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    const std::vector<char> contents{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    const auto bytes = std::as_bytes(std::span(contents));
    return std::vector<std::byte>(bytes.begin(), bytes.end());
}

} // namespace

auto OpenXrSkybox::Create(const OpenXrState& xr, AppState& app) -> std::unique_ptr<OpenXrSkybox> {
    if (!app.config.use_skybox || !xr.instance->HasEquirect2()) {
        return nullptr;
    }
    auto skybox = Load(app);
    return skybox ? std::move(*skybox) : nullptr;
}

OpenXrSkybox::OpenXrSkybox(std::shared_ptr<Image> texture) : texture_(std::move(texture)) {
}

auto OpenXrSkybox::Load(AppState& app) -> Result<std::unique_ptr<OpenXrSkybox>> {
    auto& device = *app.device;
    ASSIGN_OR_RETURN(auto command_buffer, device.CreateCommandBuffer(device.TransferQueue()));

    std::shared_ptr<Image> texture;
    if (const auto& custom = app.config.skybox_texture; !custom.empty()) {
        if (const auto contents = ReadBinaryFile(Config::Root() / custom)) {
            if (auto image = command_buffer.UploadDds(*contents)) {
                texture = std::move(*image);
            } else {
                LOG_WARN("Could not use custom skybox texture at: {}", custom);
                LOG_WARN("{}", image.error().Message());
            }
        } else {
            LOG_WARN("Could not open custom skybox texture at: {}", custom);
        }
    }
    if (!texture) {
        ASSIGN_OR_RETURN(texture, command_buffer.UploadDds(std::as_bytes(std::span(kDefaultSkybox))));
    }
    RETURN_IF_ERROR(command_buffer.SubmitUpload());
    return std::unique_ptr<OpenXrSkybox>(new OpenXrSkybox(std::move(texture)));
}

auto OpenXrSkybox::PrepareSky(const OpenXrState& xr, AppState& app, CommandBatch& batch) -> Result<> {
    if (sky_) {
        return {};
    }
    auto& device = *app.device;
    ASSIGN_OR_RETURN(auto swapchain, OpenXrSwapchain::Create(*xr.instance, xr.session, device, texture_->Extent(), true));
    ASSIGN_OR_RETURN(auto target, swapchain->AcquireWaitImage());
    ASSIGN_OR_RETURN(auto pipeline, device.CreatePipeline(Shaders::Srgb(), device.NativeFormat(), Blend::Disabled));
    ASSIGN_OR_RETURN(auto texture_set, pipeline->CreateTextureSet(0, texture_, device.TextureFilter()));
    constexpr std::array kAlpha{1.0f};
    ASSIGN_OR_RETURN(auto alpha_set, pipeline->CreateUniformSet(1, kAlpha));
    const auto pass = pipeline->CreatePassForTarget(*target, {std::move(texture_set), std::move(alpha_set)});

    ASSIGN_OR_RETURN(auto command_buffer, device.CreateCommandBuffer(device.GraphicsQueue()));
    command_buffer.BeginRendering(target);
    command_buffer.Draw(pass);
    command_buffer.EndRendering();
    batch.Push(std::move(command_buffer));

    sky_ = std::move(swapchain);
    return {};
}

auto OpenXrSkybox::PrepareGrid(const OpenXrState& xr, AppState& app, CommandBatch& batch) -> Result<> {
    if (grid_) {
        return {};
    }
    auto& device = *app.device;
    ASSIGN_OR_RETURN(
        auto swapchain,
        OpenXrSwapchain::Create(*xr.instance, xr.session, device, {.width = 1024, .height = 1024}, true)
    );
    ASSIGN_OR_RETURN(auto pipeline, device.CreatePipeline(Shaders::Grid(), device.NativeFormat(), Blend::Alpha));
    ASSIGN_OR_RETURN(auto target, swapchain->AcquireWaitImage());
    const auto pass = pipeline->CreatePassForTarget(*target, {});

    ASSIGN_OR_RETURN(auto command_buffer, device.CreateCommandBuffer(device.GraphicsQueue()));
    command_buffer.BeginRendering(target);
    command_buffer.Draw(pass);
    command_buffer.EndRendering();
    batch.Push(std::move(command_buffer));

    grid_ = std::move(swapchain);
    return {};
}

auto OpenXrSkybox::Render(const OpenXrState& xr, AppState& app, CommandBatch& batch) -> Result<> {
    RETURN_IF_ERROR(PrepareSky(xr, app, batch));
    return PrepareGrid(xr, app, batch);
}

auto OpenXrSkybox::Present(const OpenXrState& xr, const AppState& app) -> Result<std::vector<OpenXrLayer>> {
    constexpr float kPi = std::numbers::pi_v<float>;
    const Eigen::Vector3f hmd = app.input.hmd.translation();

    RETURN_IF_ERROR(sky_->EnsureImageReleased());
    const XrCompositionLayerEquirect2KHR sky{
        .type = XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR,
        .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
        .space = xr.stage,
        .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
        .subImage = sky_->SubImage(),
        .pose =
            {.orientation = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f}, .position = {.x = hmd.x(), .y = hmd.y(), .z = hmd.z()}},
        .radius = 10.0f,
        .centralHorizontalAngle = 2.0f * kPi,
        .upperVerticalAngle = 0.5f * kPi,
        .lowerVerticalAngle = -0.5f * kPi,
    };

    RETURN_IF_ERROR(grid_->EnsureImageReleased());
    const XrCompositionLayerQuad grid{
        .type = XR_TYPE_COMPOSITION_LAYER_QUAD,
        .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
        .space = xr.stage,
        .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
        .subImage = grid_->SubImage(),
        .pose = OpenXrPose::FromTranslationRotation(
            Eigen::Vector3f::Zero(),
            Eigen::Quaternionf(Eigen::AngleAxisf(kPi * -0.5f, Eigen::Vector3f::UnitX()))
        ),
        .size = {.width = 10.0f, .height = 10.0f},
    };

    return std::vector<OpenXrLayer>{sky, grid};
}
