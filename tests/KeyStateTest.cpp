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
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "Canvas.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "GpuReadback.hpp"
#include "Keyboard.hpp"
#include "Png.hpp"
#include "State.hpp"
#include "TestApp.hpp"

namespace {

constexpr uint32_t kKeyCanvasWidth = 160;
constexpr uint32_t kKeyCanvasHeight = 80;

auto Render(Canvas& canvas, std::vector<unsigned char>& pixels) -> void {
    auto& app = TestApp::Get();
    auto target = app.device->CreateRenderTexture(
        kKeyCanvasWidth,
        kKeyCanvasHeight,
        app.device->NativeFormat(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );
    ASSERT_TRUE(target.has_value());
    CommandBatch batch;
    ASSERT_TRUE(canvas.Render(app, *target, batch, 1.0f).has_value());
    ASSERT_TRUE(batch.Submit().has_value());
    app.device->GraphicsQueue().WaitIdle();
    pixels = GpuReadback::Read(*app.device, **target);
}

auto PixelAt(const std::vector<unsigned char>& pixels, uint32_t x, uint32_t y) -> Eigen::Vector4i {
    const size_t offset = ((static_cast<size_t>(y) * kKeyCanvasWidth) + x) * 4;
    return {pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]};
}

} // namespace

TEST(KeyState, ModifierAppliesToTheNextKeyOnly) {
    auto& app = TestApp::Get();
    auto state = std::make_shared<KeyboardState>();
    ModifierControl shift(state, kModShift);
    KeyControl letter(state, VirtualKey::A);

    shift.OnPress(app, PointerMode::Left);
    shift.OnRelease(app);
    EXPECT_EQ(state->modifiers & kModShift, kModShift);
    EXPECT_TRUE(shift.HighlightColor(app).has_value());
    EXPECT_FALSE(shift.Outline(app).has_value());

    letter.OnPress(app, PointerMode::Left);
    letter.OnRelease(app);
    EXPECT_EQ(state->modifiers & kModShift, 0);
    EXPECT_FALSE(shift.HighlightColor(app).has_value());

    shift.OnPress(app, PointerMode::Left);
    shift.OnRelease(app);
    shift.OnPress(app, PointerMode::Left);
    shift.OnRelease(app);
    EXPECT_EQ(state->modifiers & kModShift, 0);
    EXPECT_FALSE(shift.HighlightColor(app).has_value());
}

TEST(KeyState, LetterLabelsFollowCapsLockAndShift) {
    auto& app = TestApp::Get();
    auto state = std::make_shared<KeyboardState>();
    Control label;
    state->letters.push_back({.label = &label, .lower = "q", .upper = "Q"});
    state->RefreshLetterCase();
    KeyControl caps(state, VirtualKey::Caps);
    ModifierControl shift(state, kModShift);
    KeyControl letter(state, VirtualKey::Q);
    EXPECT_EQ(label.text, "q");

    caps.OnPress(app, PointerMode::Left);
    caps.OnRelease(app);
    EXPECT_TRUE(state->caps_lock);
    EXPECT_EQ(label.text, "Q");

    shift.OnPress(app, PointerMode::Left);
    shift.OnRelease(app);
    EXPECT_EQ(label.text, "q");

    letter.OnPress(app, PointerMode::Left);
    letter.OnRelease(app);
    EXPECT_EQ(label.text, "Q");
    EXPECT_TRUE(state->caps_lock);

    caps.OnPress(app, PointerMode::Left);
    caps.OnRelease(app);
    EXPECT_FALSE(state->caps_lock);
    EXPECT_EQ(label.text, "q");

    shift.OnPress(app, PointerMode::Left);
    shift.OnRelease(app);
    EXPECT_EQ(label.text, "Q");
}

TEST(KeyState, CapsLockLatchesWithAGlowingOutline) {
    auto& app = TestApp::Get();
    auto state = std::make_shared<KeyboardState>();
    KeyControl caps(state, VirtualKey::Caps);
    KeyControl letter(state, VirtualKey::A);

    caps.OnPress(app, PointerMode::Left);
    caps.OnRelease(app);
    EXPECT_TRUE(caps.Latched());
    EXPECT_TRUE(caps.Outline(app).has_value());
    EXPECT_TRUE(caps.HighlightColor(app).has_value());

    letter.OnPress(app, PointerMode::Left);
    letter.OnRelease(app);
    EXPECT_FALSE(letter.Latched());
    EXPECT_TRUE(caps.Latched());

    caps.OnPress(app, PointerMode::Left);
    caps.OnRelease(app);
    EXPECT_FALSE(caps.Latched());
    EXPECT_FALSE(caps.Outline(app).has_value());
}

TEST(KeyState, LockedKeyRendersBrightOutlineAroundAContrastFill) {
    auto& app = TestApp::Get();
    auto canvas = Canvas::Create(app.device, kKeyCanvasWidth, kKeyCanvasHeight, app.device->NativeFormat());
    ASSERT_TRUE(canvas.has_value());
    auto state = std::make_shared<KeyboardState>();
    (*canvas)->bg_color = Eigen::Vector4f(0.12f, 0.13f, 0.19f, 1.0f);
    (*canvas)->Panel(0.0f, 0.0f, kKeyCanvasWidth, kKeyCanvasHeight, 0.0f);
    (*canvas)->font_size = 18;
    auto& caps = (*canvas)
                     ->Button(8.0f, 8.0f, 144.0f, 64.0f, 12.0f, "Caps Lock", std::make_unique<KeyControl>(state, VirtualKey::Caps));
    caps.OnPress(app, PointerMode::Left);
    caps.OnRelease(app);
    ASSERT_TRUE((*canvas)->ShouldRender(app).has_value());

    std::vector<unsigned char> pixels;
    Render(**canvas, pixels);
    Png::Write(TestApp::OutputDir() / "caps-lock-latched.png", kKeyCanvasWidth, kKeyCanvasHeight, pixels);

    const Eigen::Vector4i edge = PixelAt(pixels, 80, 9);
    const Eigen::Vector4i inside = PixelAt(pixels, 30, 40);
    const Eigen::Vector4i outside = PixelAt(pixels, 2, 2);
    EXPECT_GT(edge.x() + edge.y() + edge.z(), inside.x() + inside.y() + inside.z() + 150);
    EXPECT_GT(inside.x() + inside.y() + inside.z(), outside.x() + outside.y() + outside.z());
}

TEST(KeyState, PressFlashesAndFadesAfterRelease) {
    auto& app = TestApp::Get();
    auto canvas = Canvas::Create(app.device, kKeyCanvasWidth, kKeyCanvasHeight, app.device->NativeFormat());
    ASSERT_TRUE(canvas.has_value());
    (*canvas)->Button(0.0f, 0.0f, kKeyCanvasWidth, kKeyCanvasHeight, 8.0f, "Key");
    std::vector<unsigned char> pixels;
    ASSERT_TRUE((*canvas)->ShouldRender(app).has_value());
    Render(**canvas, pixels);
    const Eigen::Vector4i idle = PixelAt(pixels, 20, 40);

    auto readiness = (*canvas)->ShouldRender(app);
    ASSERT_TRUE(readiness.has_value());
    EXPECT_EQ(*readiness, RenderReadiness::Can);

    const PointerHit hit{.uv = Eigen::Vector2f(0.5f, 0.5f)};
    (*canvas)->OnPointer(app, hit, true);
    readiness = (*canvas)->ShouldRender(app);
    ASSERT_TRUE(readiness.has_value());
    EXPECT_EQ(*readiness, RenderReadiness::Should);
    Render(**canvas, pixels);
    EXPECT_NE(PixelAt(pixels, 20, 40), idle);

    (*canvas)->OnPointer(app, hit, false);
    Render(**canvas, pixels);
    readiness = (*canvas)->ShouldRender(app);
    ASSERT_TRUE(readiness.has_value());
    EXPECT_EQ(*readiness, RenderReadiness::Should);

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    readiness = (*canvas)->ShouldRender(app);
    ASSERT_TRUE(readiness.has_value());
    Render(**canvas, pixels);
    EXPECT_EQ(PixelAt(pixels, 20, 40), idle);
    readiness = (*canvas)->ShouldRender(app);
    ASSERT_TRUE(readiness.has_value());
    EXPECT_EQ(*readiness, RenderReadiness::Can);
}

TEST(KeyState, CapsLockOnTheKeyboardLatchesAndUppercasesLetters) {
    auto& app = TestApp::Get();
    auto keyboard = Keyboard::Create(app, nullptr);
    ASSERT_TRUE(keyboard.has_value());
    auto& backend = *(*keyboard)->backend;
    ASSERT_TRUE(backend.Init(app).has_value());
    const auto meta = backend.Meta();
    ASSERT_TRUE(meta.has_value());
    const auto width = static_cast<float>(meta->extent.width);
    const auto height = static_cast<float>(meta->extent.height);

    const auto render = [&]() -> std::vector<unsigned char> {
        EXPECT_TRUE(backend.ShouldRender(app).has_value());
        auto target = app.device->CreateRenderTexture(
            meta->extent.width,
            meta->extent.height,
            app.device->NativeFormat(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        );
        EXPECT_TRUE(target.has_value());
        CommandBatch batch;
        EXPECT_TRUE(backend.Render(app, *target, batch, 1.0f).has_value());
        EXPECT_TRUE(batch.Submit().has_value());
        app.device->GraphicsQueue().WaitIdle();
        return GpuReadback::Read(*app.device, **target);
    };
    const auto brightness = [&](const std::vector<unsigned char>& pixels, uint32_t x, uint32_t y) -> int {
        const size_t offset = ((static_cast<size_t>(y) * meta->extent.width) + x) * 4;
        return pixels[offset] + pixels[offset + 1] + pixels[offset + 2];
    };

    const auto before = render();
    const PointerHit caps{.uv = Eigen::Vector2f(80.0f / width, 294.0f / height)};
    backend.OnPointer(app, caps, true);
    backend.OnPointer(app, caps, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const auto after = render();
    Png::Write(TestApp::OutputDir() / "keyboard-caps-lock.png", meta->extent.width, meta->extent.height, after);

    EXPECT_GT(brightness(after, 84, 262), brightness(before, 84, 262) + 150);
    EXPECT_GT(GpuReadback::DistinctColors(after), 2U);
}
