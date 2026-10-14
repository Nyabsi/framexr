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

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "CommandBatch.hpp"
#include "CommandBuffer.hpp"
#include "Config.hpp"
#include "Device.hpp"
#include "FakeCapture.hpp"
#include "GpuReadback.hpp"
#include "Compose.hpp"
#include "Png.hpp"
#include "Desktop.hpp"
#include "ScreenRenderer.hpp"
#include "TestApp.hpp"
#include "framexr_config.h"

#if FRAMEXR_FEATURE_WAYLAND
#include "PipewireCapture.hpp"
#include "ScreenCastPortal.hpp"
#endif

namespace
{

auto RenderUntilReady(ScreenRenderer& renderer, const std::string& name) -> void
{
    bool ready = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!ready && std::chrono::steady_clock::now() < deadline)
    {
        const auto readiness = renderer.ShouldRender();
        ASSERT_TRUE(readiness.has_value());
        ready = *readiness == RenderReadiness::Should;
        if (!ready)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    ASSERT_TRUE(ready);

    const auto meta = renderer.Meta();
    ASSERT_TRUE(meta.has_value());
    auto target = g_device->CreateRenderTexture(
        meta->extent.width,
        meta->extent.height,
        g_device->NativeFormat(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );
    ASSERT_TRUE(target.has_value());
    CommandBatch batch;
    const auto rendered = renderer.Render(*target, batch, 1.0f);
    ASSERT_TRUE(rendered.has_value() && *rendered);
    ASSERT_TRUE(batch.Submit().has_value());
    g_device->GraphicsQueue().WaitIdle();

    const auto pixels = GpuReadback::Read(**target);
    EXPECT_EQ(pixels.size(), size_t{meta->extent.width} * meta->extent.height * 4);
    EXPECT_TRUE(GpuReadback::IsOpaque(pixels));
    Png::Write(TestApp::OutputDir() / std::format("{}.png", name), meta->extent.width, meta->extent.height, pixels);
}

} // namespace

TEST(ScreenCapture, ScreenPipelineBlendsLikeOriginal)
{
    constexpr uint32_t kSize = 64;
    constexpr unsigned char kGray = 128;
    std::vector<std::byte> bgra(size_t{kSize} * kSize * 4, std::byte{kGray});
    ScreenRenderer renderer("fake", std::make_unique<FakeCapture>(kSize, kSize, std::move(bgra)));
    RenderUntilReady(renderer, "fake-capture");

    auto target = g_device->CreateRenderTexture(kSize, kSize, g_device->NativeFormat(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    ASSERT_TRUE(target.has_value());
    CommandBatch batch;
    ASSERT_TRUE(renderer.ShouldRender().has_value());
    const auto rendered = renderer.Render(*target, batch, 1.0f);
    ASSERT_TRUE(rendered.has_value());
    ASSERT_TRUE(*rendered);
    ASSERT_TRUE(batch.Submit().has_value());
    g_device->GraphicsQueue().WaitIdle();

    const auto pixels = GpuReadback::Read(**target);
    ASSERT_FALSE(pixels.empty());
    const double sampled = kGray / 255.0;
    const double squared = sampled * sampled;
    const double encoded = squared <= 0.0031308 ? 12.92 * squared : 1.055 * std::pow(squared, 1.0 / 2.4) - 0.055;
    const int expected = static_cast<int>(std::lround(encoded * 255.0));
    const size_t center = (size_t{kSize} / 2 * kSize + kSize / 2) * 4;
    for (size_t channel = 0; channel < 3; ++channel)
    {
        EXPECT_NEAR(pixels[center + channel], expected, 1) << "channel " << channel;
    }
}

TEST(ScreenCapture, LowpassFilterSoftensOnlyMinifiedScreens)
{
    constexpr uint32_t kSize = 64;
    std::vector<std::byte> stripes(size_t{kSize} * kSize * 4);
    for (size_t y = 0; y < kSize; ++y)
    {
        for (size_t x = 0; x < kSize; ++x)
        {
            const auto value = x % 2 == 0 ? std::byte{255} : std::byte{0};
            const size_t offset = (y * kSize + x) * 4;
            stripes[offset] = value;
            stripes[offset + 1] = value;
            stripes[offset + 2] = value;
            stripes[offset + 3] = std::byte{255};
        }
    }
    ScreenRenderer renderer("stripes", std::make_unique<FakeCapture>(kSize, kSize, std::move(stripes)));
    RenderUntilReady(renderer, "stripes");

    const auto render = [&](const TexelFootprint& footprint) -> std::vector<unsigned char>
    {
        renderer.SetTexelFootprint(footprint);
        bool ready = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!ready && std::chrono::steady_clock::now() < deadline)
        {
            const auto readiness = renderer.ShouldRender();
            ready = readiness && *readiness == RenderReadiness::Should;
            if (!ready)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
        EXPECT_TRUE(ready);
        auto target = g_device->CreateRenderTexture(kSize, kSize, g_device->NativeFormat(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        EXPECT_TRUE(target.has_value());
        CommandBatch batch;
        const auto rendered = renderer.Render(*target, batch, 1.0f);
        EXPECT_TRUE(rendered.has_value() && *rendered);
        EXPECT_TRUE(batch.Submit().has_value());
        g_device->GraphicsQueue().WaitIdle();
        return GpuReadback::Read(**target);
    };
    const auto contrast = [](const std::vector<unsigned char>& pixels, size_t x) -> int
    {
        const size_t row = size_t{kSize} / 2 * kSize;
        return std::abs(static_cast<int>(pixels[(row + x) * 4]) - static_cast<int>(pixels[(row + x + 1) * 4]));
    };
    const auto facing = [](float distance) -> TexelFootprint
    {
        return {
            .eye = Eigen::Vector3f(0.0f, 0.0f, distance),
            .size = Eigen::Vector2f::Ones(),
            .display_pixels_per_radian = Eigen::Vector2f::Constant(static_cast<float>(kSize)),
        };
    };

    const int sharp = contrast(render(facing(0.9f)), 32);
    const int soft = contrast(render(facing(2.5f)), 32);
    g_config->screen_lowpass_filter = false;
    const int unfiltered = contrast(render(facing(2.5f)), 32);
    g_config->screen_lowpass_filter = true;

    EXPECT_GT(sharp, 200);
    EXPECT_LT(soft, sharp / 4);
    EXPECT_GT(unfiltered, 200);

    TexelFootprint slanted = facing(0.3f);
    slanted.eye.x() = -0.45f;
    const auto pixels = render(slanted);
    EXPECT_GT(contrast(pixels, 4), 200);
    EXPECT_LT(contrast(pixels, 58), 60);
}

#if FRAMEXR_FEATURE_WAYLAND
TEST(ScreenCapture, PipewireFrameGoesThroughScreenPipeline)
{
    const char* token = std::getenv("FRAMEXR_TEST_PW_TOKEN");
    if (token == nullptr)
    {
        GTEST_SKIP() << "set FRAMEXR_TEST_PW_TOKEN to a ScreenCast restore token";
    }
    auto selection = ScreenCastPortal::SelectScreen({.restore_token = token, .screens_only = true, .persist = true}).get();
    ASSERT_TRUE(selection.has_value()) << selection.error().Message();
    ASSERT_FALSE(selection->streams.empty());
    ScreenRenderer renderer("pipewire", std::make_unique<PipewireCapture>("pipewire", selection->streams.front().node_id));
    RenderUntilReady(renderer, "pipewire-capture");
}
#endif

#if FRAMEXR_FEATURE_X11
#include "XshmCapture.hpp"

TEST(ScreenCapture, XshmFrameGoesThroughScreenPipeline)
{
    auto monitors = Xshm::Monitors();
    if (!monitors || monitors->empty())
    {
        GTEST_SKIP() << "no X11 display";
    }
    const auto screen = monitors->front();
    ScreenRenderer renderer(screen->name, std::make_unique<XshmCapture>(screen));
    RenderUntilReady(renderer, "xshm-capture");
}
#endif
