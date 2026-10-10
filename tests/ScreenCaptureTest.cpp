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
#include <vector>
#include <cstdlib>
#include <format>
#include <string>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "FakeCapture.hpp"
#include "GpuReadback.hpp"
#include "Png.hpp"
#include "Screen.hpp"
#include "State.hpp"
#include "TestApp.hpp"
#include "framexr_config.h"

#if FRAMEXR_FEATURE_WAYLAND
#include "PipewireCapture.hpp"
#include "ScreenCastPortal.hpp"
#endif

namespace {

auto RenderUntilReady(ScreenRenderer& renderer, const std::string& name) -> void {
    auto& app = TestApp::Get();
    bool ready = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!ready && std::chrono::steady_clock::now() < deadline) {
        const auto readiness = renderer.ShouldRender(app);
        ASSERT_TRUE(readiness.has_value());
        ready = *readiness == RenderReadiness::Should;
        if (!ready) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    ASSERT_TRUE(ready);

    const auto meta = renderer.Meta();
    ASSERT_TRUE(meta.has_value());
    auto target = app.device->CreateRenderTexture(
        meta->extent.width,
        meta->extent.height,
        app.device->NativeFormat(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );
    ASSERT_TRUE(target.has_value());
    CommandBatch batch;
    const auto rendered = renderer.Render(app, *target, batch, 1.0f);
    ASSERT_TRUE(rendered.has_value() && *rendered);
    ASSERT_TRUE(batch.Submit().has_value());
    app.device->GraphicsQueue().WaitIdle();

    const auto pixels = GpuReadback::Read(*app.device, **target);
    EXPECT_EQ(pixels.size(), size_t{meta->extent.width} * meta->extent.height * 4);
    EXPECT_TRUE(GpuReadback::IsOpaque(pixels));
    Png::Write(TestApp::OutputDir() / std::format("{}.png", name), meta->extent.width, meta->extent.height, pixels);
}

} // namespace

TEST(ScreenCapture, ScreenPipelineBlendsLikeOriginal) {
    constexpr uint32_t kSize = 64;
    constexpr unsigned char kGray = 128;
    std::vector<std::byte> bgra(size_t{kSize} * kSize * 4, std::byte{kGray});
    ScreenRenderer renderer("fake", std::make_unique<FakeCapture>(kSize, kSize, std::move(bgra)));
    RenderUntilReady(renderer, "fake-capture");

    auto& app = TestApp::Get();
    auto target = app.device->CreateRenderTexture(kSize, kSize, app.device->NativeFormat(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    ASSERT_TRUE(target.has_value());
    CommandBatch batch;
    ASSERT_TRUE(renderer.ShouldRender(app).has_value());
    const auto rendered = renderer.Render(app, *target, batch, 1.0f);
    ASSERT_TRUE(rendered.has_value());
    ASSERT_TRUE(*rendered);
    ASSERT_TRUE(batch.Submit().has_value());
    app.device->GraphicsQueue().WaitIdle();

    const auto pixels = GpuReadback::Read(*app.device, **target);
    ASSERT_FALSE(pixels.empty());
    const double sampled = kGray / 255.0;
    const double squared = sampled * sampled;
    const double encoded = squared <= 0.0031308 ? 12.92 * squared : 1.055 * std::pow(squared, 1.0 / 2.4) - 0.055;
    const int expected = static_cast<int>(std::lround(encoded * 255.0));
    const size_t center = (size_t{kSize} / 2 * kSize + kSize / 2) * 4;
    for (size_t channel = 0; channel < 3; ++channel) {
        EXPECT_NEAR(pixels[center + channel], expected, 1) << "channel " << channel;
    }
}

#if FRAMEXR_FEATURE_WAYLAND
TEST(ScreenCapture, PipewireFrameGoesThroughScreenPipeline) {
    const char* token = std::getenv("FRAMEXR_TEST_PW_TOKEN");
    if (token == nullptr) {
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

TEST(ScreenCapture, XshmFrameGoesThroughScreenPipeline) {
    auto monitors = Xshm::Monitors();
    if (!monitors || monitors->empty()) {
        GTEST_SKIP() << "no X11 display";
    }
    const auto screen = monitors->front();
    ScreenRenderer renderer(screen->name, std::make_unique<XshmCapture>(screen));
    RenderUntilReady(renderer, "xshm-capture");
}
#endif
