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
#include <array>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "ButtonComponent.hpp"
#include "Compose.hpp"
#include "CommandBatch.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "Form.hpp"
#include "GpuReadback.hpp"
#include "KeyComponent.hpp"
#include "Keyboard.hpp"
#include "LabelComponent.hpp"
#include "PanelComponent.hpp"
#include "Png.hpp"
#include "TestApp.hpp"

namespace {

constexpr uint32_t kKeyCanvasWidth = 160;
constexpr uint32_t kKeyCanvasHeight = 80;

auto Render(Compose& canvas, std::vector<unsigned char>& pixels) -> void {
    auto target = g_device->CreateRenderTexture(
        kKeyCanvasWidth,
        kKeyCanvasHeight,
        g_device->NativeFormat(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );
    ASSERT_TRUE(target.has_value());
    CommandBatch batch;
    ASSERT_TRUE(canvas.Render(*target, batch, 1.0f).has_value());
    ASSERT_TRUE(batch.Submit().has_value());
    g_device->GraphicsQueue().WaitIdle();
    pixels = GpuReadback::Read(**target);
}

auto PixelAt(const std::vector<unsigned char>& pixels, uint32_t x, uint32_t y) -> Eigen::Vector4i {
    const size_t offset = ((static_cast<size_t>(y) * kKeyCanvasWidth) + x) * 4;
    return {pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]};
}

} // namespace

TEST(KeyState, ModifierAppliesToTheNextKeyOnly) {
    KeyboardInput input;
    const auto tap = [&input](VirtualKey key) -> void {
        input.Press(key, PointerMode::Left);
        input.Release(key);
    };

    tap(VirtualKey::LShift);
    EXPECT_TRUE(input.Holds(kModShift));
    tap(VirtualKey::A);
    EXPECT_FALSE(input.Holds(kModShift));
    tap(VirtualKey::LShift);
    tap(VirtualKey::LShift);
    EXPECT_FALSE(input.Holds(kModShift));
}

TEST(KeyState, UppercaseFollowsCapsLockAndShift) {
    KeyboardInput input;
    const auto tap = [&input](VirtualKey key) -> void {
        input.Press(key, PointerMode::Left);
        input.Release(key);
    };
    EXPECT_FALSE(input.Uppercase());

    tap(VirtualKey::Caps);
    EXPECT_TRUE(input.CapsLock());
    EXPECT_TRUE(input.Uppercase());
    tap(VirtualKey::LShift);
    EXPECT_FALSE(input.Uppercase());
    tap(VirtualKey::Q);
    EXPECT_TRUE(input.Uppercase());
    EXPECT_TRUE(input.CapsLock());
    tap(VirtualKey::Caps);
    EXPECT_FALSE(input.CapsLock());
    EXPECT_FALSE(input.Uppercase());
    tap(VirtualKey::LShift);
    EXPECT_TRUE(input.Uppercase());
}

TEST(KeyState, CapsLockLatchesUntilPressedAgain) {
    KeyboardInput input;
    const auto tap = [&input](VirtualKey key) -> void {
        input.Press(key, PointerMode::Left);
        input.Release(key);
    };
    tap(VirtualKey::Caps);
    EXPECT_TRUE(input.Latched(VirtualKey::Caps));
    tap(VirtualKey::A);
    EXPECT_FALSE(input.Latched(VirtualKey::A));
    EXPECT_TRUE(input.Latched(VirtualKey::Caps));
    tap(VirtualKey::Caps);
    EXPECT_FALSE(input.Latched(VirtualKey::Caps));
}

TEST(KeyState, LockedKeyRendersBrightOutlineAroundAContrastFill) {
    auto keyboard = Keyboard::Create(nullptr);
    ASSERT_TRUE(keyboard.has_value());
    auto& input = (*keyboard)->Input();
    input.Press(VirtualKey::Caps, PointerMode::Left);
    input.Release(VirtualKey::Caps);

    auto form = std::make_unique<Form>("caps");
    form->SetSize({kKeyCanvasWidth, kKeyCanvasHeight});
    auto& background = form->AddComponent<PanelComponent>();
    background.SetSize({kKeyCanvasWidth, kKeyCanvasHeight});
    background.SetCornerRadius(0.0f);
    background.SetBgColor(Eigen::Vector4f(0.12f, 0.13f, 0.19f, 1.0f));
    auto& caps = form->AddComponent<KeyComponent>();
    caps.SetRect({8.0f, 8.0f, 144.0f, 64.0f});
    caps.SetCornerRadius(12.0f);
    caps.SetFontSize(18);
    caps.SetBgColor(Eigen::Vector4f::Zero());
    caps.SetLegends(KeyCapType::Regular, std::array<std::string, 1>{"Caps Lock"});
    caps.SetHighlightColor([&input]() -> std::optional<Eigen::Vector4f> {
        return input.Latched(VirtualKey::Caps) ? std::optional(Eigen::Vector4f(1.0f, 1.0f, 1.0f, 0.16f)) : std::nullopt;
    });
    caps.SetOutline([&input]() -> std::optional<HighlightOutline> {
        return input.Latched(VirtualKey::Caps)
                 ? std::optional(HighlightOutline{.color = {198.0f / 255.0f, 160.0f / 255.0f, 246.0f / 255.0f, 1.0f}, .width = 3.0f})
                 : std::nullopt;
    });
    auto canvas = Compose::Create(std::move(form));
    ASSERT_TRUE(canvas.has_value());
    ASSERT_TRUE((*canvas)->ShouldRender().has_value());

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
    auto form = std::make_unique<Form>("key");
    form->SetSize({kKeyCanvasWidth, kKeyCanvasHeight});
    auto& key = form->AddComponent<ButtonComponent>("", "Key");
    key.SetSize({kKeyCanvasWidth, kKeyCanvasHeight});
    key.SetCornerRadius(8.0f);
    auto canvas = Compose::Create(std::move(form));
    ASSERT_TRUE(canvas.has_value());
    std::vector<unsigned char> pixels;
    ASSERT_TRUE((*canvas)->ShouldRender().has_value());
    Render(**canvas, pixels);
    const Eigen::Vector4i idle = PixelAt(pixels, 20, 40);

    auto readiness = (*canvas)->ShouldRender();
    ASSERT_TRUE(readiness.has_value());
    EXPECT_EQ(*readiness, RenderReadiness::Can);

    const PointerHit hit{.uv = Eigen::Vector2f(0.5f, 0.5f)};
    (*canvas)->OnPointer(hit, true);
    readiness = (*canvas)->ShouldRender();
    ASSERT_TRUE(readiness.has_value());
    EXPECT_EQ(*readiness, RenderReadiness::Should);
    Render(**canvas, pixels);
    EXPECT_NE(PixelAt(pixels, 20, 40), idle);

    (*canvas)->OnPointer(hit, false);
    Render(**canvas, pixels);
    readiness = (*canvas)->ShouldRender();
    ASSERT_TRUE(readiness.has_value());
    EXPECT_EQ(*readiness, RenderReadiness::Should);

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    readiness = (*canvas)->ShouldRender();
    ASSERT_TRUE(readiness.has_value());
    Render(**canvas, pixels);
    EXPECT_EQ(PixelAt(pixels, 20, 40), idle);
    readiness = (*canvas)->ShouldRender();
    ASSERT_TRUE(readiness.has_value());
    EXPECT_EQ(*readiness, RenderReadiness::Can);
}

TEST(KeyState, CapsLockOnTheKeyboardLatchesAndUppercasesLetters) {
    auto keyboard = Keyboard::Create(nullptr);
    ASSERT_TRUE(keyboard.has_value());
    auto& backend = *(*keyboard)->Composer();
    const auto meta = backend.Meta();
    ASSERT_TRUE(meta.has_value());
    const auto width = static_cast<float>(meta->extent.width);
    const auto height = static_cast<float>(meta->extent.height);

    const auto render = [&]() -> std::vector<unsigned char> {
        EXPECT_TRUE(backend.ShouldRender().has_value());
        auto target = g_device->CreateRenderTexture(
            meta->extent.width,
            meta->extent.height,
            g_device->NativeFormat(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        );
        EXPECT_TRUE(target.has_value());
        CommandBatch batch;
        EXPECT_TRUE(backend.Render(*target, batch, 1.0f).has_value());
        EXPECT_TRUE(batch.Submit().has_value());
        g_device->GraphicsQueue().WaitIdle();
        return GpuReadback::Read(**target);
    };
    const auto brightness = [&](const std::vector<unsigned char>& pixels, uint32_t x, uint32_t y) -> int {
        const size_t offset = ((static_cast<size_t>(y) * meta->extent.width) + x) * 4;
        return pixels[offset] + pixels[offset + 1] + pixels[offset + 2];
    };

    const auto before = render();
    const PointerHit caps{.uv = Eigen::Vector2f(80.0f / width, 294.0f / height)};
    backend.OnPointer(caps, true);
    backend.OnPointer(caps, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const auto after = render();
    Png::Write(TestApp::OutputDir() / "keyboard-caps-lock.png", meta->extent.width, meta->extent.height, after);

    EXPECT_GT(brightness(after, 84, 262), brightness(before, 84, 262) + 150);
    EXPECT_GT(GpuReadback::DistinctColors(after), 2U);
}

TEST(KeyState, TheLegendThatWillBeTypedIsTheBrightOne) {
    auto form = std::make_unique<Form>("key");
    form->SetSize({kKeyCanvasWidth, kKeyCanvasHeight});
    auto& key = form->AddComponent<KeyComponent>();
    key.SetSize({kKeyCanvasWidth, kKeyCanvasHeight});
    key.SetFgColor(Eigen::Vector4f::Ones());
    const std::vector<std::string> labels{"1", "!"};
    const std::vector<std::string> ime{"ㅂ", "ㅃ"};
    key.SetLegends(KeyCapType::Reversed, labels, ime);
    ASSERT_EQ(key.Legends().size(), 4U);
    const auto bright = [&key]() -> std::string {
        std::string found;
        for (const auto* legend : key.Legends()) {
            if (legend->FgColor().w() > 0.99f) {
                found += legend->Text();
            }
        }
        return found;
    };

    EXPECT_EQ(bright(), "1");
    key.Emphasize({.shifted = true});
    EXPECT_EQ(bright(), "!");
    key.Emphasize({.native = true});
    EXPECT_EQ(bright(), "ㅂ");
    key.Emphasize({.shifted = true, .native = true});
    EXPECT_EQ(bright(), "ㅃ");
}
