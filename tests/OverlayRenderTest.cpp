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

#include "Compose.hpp"
#include "Form.hpp"
#include "PanelComponent.hpp"
#include "CommandBatch.hpp"
#include "CommandBuffer.hpp"
#include "Config.hpp"
#include "Settings.hpp"
#include "Device.hpp"
#include "GpuReadback.hpp"
#include "Keyboard.hpp"
#include "Keymap.hpp"
#include "Container.hpp"
#include "Png.hpp"
#include "TestApp.hpp"
#include "Watch.hpp"

namespace {

auto RenderPixels(Compose& backend, std::vector<unsigned char>& pixels) -> void {
    const auto readiness = backend.ShouldRender();
    ASSERT_TRUE(readiness.has_value());
    EXPECT_EQ(*readiness, RenderReadiness::Should);
    const auto meta = backend.Meta();
    ASSERT_TRUE(meta.has_value());

    auto target = g_device->CreateRenderTexture(
        meta->extent.width,
        meta->extent.height,
        g_device->NativeFormat(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );
    ASSERT_TRUE(target.has_value());
    CommandBatch batch;
    const auto rendered = backend.Render(*target, batch, 1.0f);
    ASSERT_TRUE(rendered.has_value());
    EXPECT_TRUE(*rendered);
    ASSERT_TRUE(batch.Submit().has_value());
    g_device->GraphicsQueue().WaitIdle();

    pixels = GpuReadback::Read(**target);
    ASSERT_FALSE(pixels.empty());
}

auto RenderOverlay(View& overlay, const std::string& name) -> void {
    std::vector<unsigned char> pixels;
    RenderPixels(*overlay.Composer(), pixels);
    EXPECT_GT(GpuReadback::DistinctColors(pixels), 2U);
    const auto meta = overlay.Composer()->Meta();
    ASSERT_TRUE(meta.has_value());
    Png::Write(TestApp::OutputDir() / std::format("{}.png", name), meta->extent.width, meta->extent.height, pixels);
}

constexpr uint32_t kCornerCanvasSize = 64;

auto AlphaAt(const std::vector<unsigned char>& pixels, uint32_t x, uint32_t y) -> int {
    return pixels[((static_cast<size_t>(y) * kCornerCanvasSize) + x) * 4 + 3];
}

} // namespace

TEST(OverlayRender, Watch) {
    const auto overlays = Container::CreateEmpty();
    auto watch = Watch::Create();
    ASSERT_TRUE(watch.has_value());
    EXPECT_TRUE((*watch)->WantVisible());
    RenderOverlay(**watch, "watch");
}

TEST(OverlayRender, KeyboardWithSessionKeymap) {
    std::shared_ptr<const Keymap> keymap;
    if (auto loaded = Keymap::FromWayland()) {
        keymap = std::make_shared<const Keymap>(std::move(*loaded));
    }
    auto keyboard = Keyboard::Create(keymap);
    ASSERT_TRUE(keyboard.has_value());
    RenderOverlay(**keyboard, "keyboard");
}

TEST(OverlayRender, KeyboardWithFinnishRegion) {
    g_config->keyboard_region = "fi";
    auto keyboard = Keyboard::Create(nullptr);
    g_config->keyboard_region.clear();
    ASSERT_TRUE(keyboard.has_value());
    RenderOverlay(**keyboard, "keyboard-fi");
}

TEST(OverlayRender, ImeKeyboards) {
    for (const auto* name : {"jis-full-size", "jis-tkl", "korean-full-size", "korean-tkl", "chinese-full-size", "chinese-tkl"}) {
        g_config->keyboard_profile = name;
        auto keyboard = Keyboard::Create(nullptr);
        ASSERT_TRUE(keyboard.has_value()) << name;
        RenderOverlay(**keyboard, std::format("keyboard-{}", name));
    }
    g_config->keyboard_profile = "keychron-v1-ansi";
}

TEST(OverlayRender, SettingsPanel) {
    const auto overlays = Container::CreateEmpty();
    auto settings = Settings::Create("settings");
    ASSERT_NE(settings, nullptr);
    RenderOverlay(*settings, "settings");
}

TEST(OverlayRender, RoundedCornersUseTheirRadiusAndAreSmooth) {
    auto form = std::make_unique<Form>("corner");
    form->SetSize({kCornerCanvasSize, kCornerCanvasSize});
    auto& panel = form->AddComponent<PanelComponent>();
    panel.SetSize({kCornerCanvasSize, kCornerCanvasSize});
    panel.SetCornerRadius(16.0f);
    panel.SetBgColor(Eigen::Vector4f::Ones());
    auto canvas = Compose::Create(std::move(form));
    ASSERT_TRUE(canvas.has_value());

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
