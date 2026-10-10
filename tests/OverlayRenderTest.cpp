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

#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Canvas.hpp"
#include "CommandBuffer.hpp"
#include "CustomUi.hpp"
#include "Device.hpp"
#include "GpuReadback.hpp"
#include "Keyboard.hpp"
#include "Keymap.hpp"
#include "Png.hpp"
#include "State.hpp"
#include "TestApp.hpp"
#include "Watch.hpp"

namespace {

auto RenderPixels(OverlayBackend& backend, std::vector<unsigned char>& pixels) -> void {
    auto& app = TestApp::Get();
    ASSERT_TRUE(backend.Init(app).has_value());
    const auto readiness = backend.ShouldRender(app);
    ASSERT_TRUE(readiness.has_value());
    EXPECT_EQ(*readiness, RenderReadiness::Should);
    const auto meta = backend.Meta();
    ASSERT_TRUE(meta.has_value());

    auto target = app.device->CreateRenderTexture(
        meta->extent.width,
        meta->extent.height,
        app.device->NativeFormat(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );
    ASSERT_TRUE(target.has_value());
    CommandBatch batch;
    const auto rendered = backend.Render(app, *target, batch, 1.0f);
    ASSERT_TRUE(rendered.has_value());
    EXPECT_TRUE(*rendered);
    ASSERT_TRUE(batch.Submit().has_value());
    app.device->GraphicsQueue().WaitIdle();

    pixels = GpuReadback::Read(*app.device, **target);
    ASSERT_FALSE(pixels.empty());
}

auto RenderOverlay(Overlay& overlay, const std::string& name) -> void {
    std::vector<unsigned char> pixels;
    RenderPixels(*overlay.backend, pixels);
    EXPECT_GT(GpuReadback::DistinctColors(pixels), 2U);
    const auto meta = overlay.backend->Meta();
    ASSERT_TRUE(meta.has_value());
    Png::Write(TestApp::OutputDir() / std::format("{}.png", name), meta->extent.width, meta->extent.height, pixels);
}

constexpr uint32_t kCornerCanvasSize = 64;

auto AlphaAt(const std::vector<unsigned char>& pixels, uint32_t x, uint32_t y) -> int {
    return pixels[((static_cast<size_t>(y) * kCornerCanvasSize) + x) * 4 + 3];
}

} // namespace

TEST(OverlayRender, Watch) {
    auto watch = Watch::Create(TestApp::Get());
    ASSERT_TRUE(watch.has_value());
    EXPECT_TRUE((*watch)->state.want_visible);
    RenderOverlay(**watch, "watch");
}

TEST(OverlayRender, KeyboardWithSessionKeymap) {
    std::shared_ptr<const Keymap> keymap;
    if (auto loaded = Keymap::FromWayland()) {
        keymap = std::make_shared<const Keymap>(std::move(*loaded));
    }
    auto keyboard = Keyboard::Create(TestApp::Get(), keymap);
    ASSERT_TRUE(keyboard.has_value());
    RenderOverlay(**keyboard, "keyboard");
}

TEST(OverlayRender, KeyboardWithFinnishRegion) {
    auto& app = TestApp::Get();
    app.config.keyboard_region = "fi";
    auto keyboard = Keyboard::Create(app, nullptr);
    app.config.keyboard_region.clear();
    ASSERT_TRUE(keyboard.has_value());
    RenderOverlay(**keyboard, "keyboard-fi");
}

TEST(OverlayRender, SettingsPanel) {
    auto settings = CustomUi::Create(TestApp::Get(), "settings");
    ASSERT_TRUE(settings.has_value());
    Overlay overlay(std::move(settings->first), std::move(settings->second));
    RenderOverlay(overlay, "settings");
}

TEST(OverlayRender, RoundedCornersUseTheirRadiusAndAreSmooth) {
    auto& app = TestApp::Get();
    auto canvas = Canvas::Create(app.device, kCornerCanvasSize, kCornerCanvasSize, app.device->NativeFormat());
    ASSERT_TRUE(canvas.has_value());
    (*canvas)->bg_color = Eigen::Vector4f::Ones();
    (*canvas)->Panel(0.0f, 0.0f, kCornerCanvasSize, kCornerCanvasSize, 16.0f);

    std::vector<unsigned char> pixels;
    RenderPixels(**canvas, pixels);
    ASSERT_EQ(pixels.size(), static_cast<size_t>(kCornerCanvasSize) * kCornerCanvasSize * 4);

    EXPECT_EQ(AlphaAt(pixels, 0, 0), 0);
    EXPECT_EQ(AlphaAt(pixels, kCornerCanvasSize - 1, kCornerCanvasSize - 1), 0);
    EXPECT_EQ(AlphaAt(pixels, 3, 3), 0);
    EXPECT_EQ(AlphaAt(pixels, kCornerCanvasSize / 2, kCornerCanvasSize / 2), 255);
    EXPECT_EQ(AlphaAt(pixels, kCornerCanvasSize / 2, 0), 255);
    EXPECT_GT(AlphaAt(pixels, 7, 7), 200);

    bool partial = false;
    for (uint32_t i = 0; i < 16; ++i) {
        const int alpha = AlphaAt(pixels, i, i);
        partial = partial || (alpha > 0 && alpha < 255);
    }
    EXPECT_TRUE(partial);
}
