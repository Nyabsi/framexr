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

#include <deque>
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
#include "ChatboxPreview.hpp"
#include "Device.hpp"
#include "GpuReadback.hpp"
#include "Keyboard.hpp"
#include "Keymap.hpp"
#include "Container.hpp"
#include "Osc.hpp"
#include "TaskQueue.hpp"
#include "Toast.hpp"
#include "UdpSocket.hpp"
#include "Png.hpp"
#include "TestApp.hpp"
#include "Watch.hpp"

namespace
{

auto RenderPixels(Compose& backend, std::vector<unsigned char>& pixels) -> void
{
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

auto RenderOverlay(View& overlay, const std::string& name) -> void
{
    std::vector<unsigned char> pixels;
    RenderPixels(*overlay.Composer(), pixels);
    EXPECT_GT(GpuReadback::DistinctColors(pixels), 2U);
    const auto meta = overlay.Composer()->Meta();
    ASSERT_TRUE(meta.has_value());
    Png::Write(TestApp::OutputDir() / std::format("{}.png", name), meta->extent.width, meta->extent.height, pixels);
}

auto CreateOverlays(Container& overlays) -> void
{
    std::deque<Task> due;
    g_tasks->TakeDue(due);
    for (auto& task : due)
    {
        if (auto* create = std::get_if<CreateOverlayTask>(&task);
            create != nullptr && overlays.BySelector(create->selector) == nullptr)
        {
            if (auto made = create->create())
            {
                overlays.Add(std::move(made));
            }
        }
    }
}

constexpr uint32_t kCornerCanvasSize = 64;

auto AlphaAt(const std::vector<unsigned char>& pixels, uint32_t x, uint32_t y) -> int
{
    return pixels[((static_cast<size_t>(y) * kCornerCanvasSize) + x) * 4 + 3];
}

} // namespace

TEST(OverlayRender, Watch)
{
    const auto overlays = Container::CreateEmpty();
    auto watch = Watch::Create();
    ASSERT_TRUE(watch.has_value());
    EXPECT_TRUE((*watch)->WantVisible());
    RenderOverlay(**watch, "watch");
}

TEST(OverlayRender, KeyboardWithSessionKeymap)
{
    std::shared_ptr<const Keymap> keymap;
    if (auto loaded = Keymap::FromWayland())
    {
        keymap = std::make_shared<const Keymap>(std::move(*loaded));
    }
    auto keyboard = Keyboard::Create(keymap);
    ASSERT_TRUE(keyboard.has_value());
    RenderOverlay(**keyboard, "keyboard");
}

TEST(OverlayRender, KeyboardWithFinnishRegion)
{
    g_config->keyboard_region = "fi";
    auto keyboard = Keyboard::Create(nullptr);
    g_config->keyboard_region.clear();
    ASSERT_TRUE(keyboard.has_value());
    RenderOverlay(**keyboard, "keyboard-fi");
}

TEST(OverlayRender, ImeKeyboards)
{
    for (const auto* name : {"jis-full-size", "jis-tkl", "korean-full-size", "korean-tkl", "chinese-full-size", "chinese-tkl"})
    {
        g_config->keyboard_profile = name;
        auto keyboard = Keyboard::Create(nullptr);
        ASSERT_TRUE(keyboard.has_value()) << name;
        RenderOverlay(**keyboard, std::format("keyboard-{}", name));
    }
    g_config->keyboard_profile = "keychron-v1-ansi";
}

TEST(OverlayRender, LongToastWrapsIntoLines)
{
    const auto overlays = Container::CreateEmpty();
    std::deque<Task> stale;
    g_tasks->TakeDue(stale);
    Toast::Show({
        .title = "VRCX",
        .body = "Someone has invited you to their instance in a world with a rather long name, and also left a message "
                "that keeps going for a while so that it needs several lines to fit.",
    });
    CreateOverlays(*overlays);
    auto* toast = overlays->ByName("toast");
    ASSERT_NE(toast, nullptr);
    ASSERT_TRUE(toast->Composer()->ShouldRender().has_value());
    const auto meta = toast->Composer()->Meta();
    ASSERT_TRUE(meta.has_value());
    EXPECT_LE(meta->extent.width, 560U);
    EXPECT_GT(meta->extent.height, 100U);
    RenderOverlay(*toast, "toast-wrapped");
}

TEST(OverlayRender, ChatboxPreviewFloatsAboveTheKeyboard)
{
    uint16_t port = 0;
    {
        auto closed = UdpSocket::Bind(UdpAddress::Loopback, 0);
        ASSERT_TRUE(closed.has_value());
        port = closed->LocalPort();
    }
    auto osc = Osc::Create(port);
    ASSERT_TRUE(osc.has_value());
    g_osc = osc->get();
    const auto overlays = Container::CreateEmpty();
    auto created = Keyboard::Create(nullptr);
    ASSERT_TRUE(created.has_value());
    (*created)->SetWantVisible(true);
    overlays->Add(std::move(*created));
    auto& keyboard = dynamic_cast<Keyboard&>(*overlays->ByName(kKeyboardName));
    ASSERT_TRUE(keyboard.Composer()->ShouldRender().has_value());
    keyboard.Composer()->FindText("Chat")->Parent()->OnPress(PointerMode::Left);
    CreateOverlays(*overlays);
    auto* preview = overlays->ByName(kChatboxPreviewName);
    ASSERT_NE(preview, nullptr);
    for (const auto key :
         {VirtualKey::H,
          VirtualKey::E,
          VirtualKey::Y,
          VirtualKey::Space,
          VirtualKey::T,
          VirtualKey::H,
          VirtualKey::E,
          VirtualKey::R,
          VirtualKey::E})
    {
        keyboard.Input().Press(key, PointerMode::Left);
        keyboard.Input().Release(key);
    }
    preview->Update();
    ASSERT_TRUE(preview->Composer()->ShouldRender().has_value());
    EXPECT_NE(preview->Composer()->FindText("hey there"), nullptr);
    EXPECT_GT(preview->Transform().translation().y(), keyboard.Transform().translation().y());
    EXPECT_FALSE(preview->Curvature().has_value());
    keyboard.SetCurvature(0.2f);
    preview->Update();
    ASSERT_TRUE(preview->Curvature().has_value());
    const float ratio = preview->Composer()->Root()->Size().x() / keyboard.Composer()->Root()->Size().x();
    EXPECT_FLOAT_EQ(*preview->Curvature(), 0.2f * ratio);
    const float keyboard_radius = keyboard.Transform().linear().col(0).norm() / *keyboard.Curvature();
    const float preview_radius = preview->Transform().linear().col(0).norm() / *preview->Curvature();
    EXPECT_NEAR(preview_radius, keyboard_radius, 1e-4f);
    RenderOverlay(*preview, "chatbox-preview");
    keyboard.Composer()->FindText("Chat")->Parent()->OnPress(PointerMode::Left);
    g_osc = nullptr;
}

TEST(OverlayRender, SettingsPanel)
{
    const auto overlays = Container::CreateEmpty();
    auto settings = Settings::Create("settings");
    ASSERT_NE(settings, nullptr);
    RenderOverlay(*settings, "settings");
}

TEST(OverlayRender, RoundedCornersUseTheirRadiusAndAreSmooth)
{
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
    for (uint32_t i = 0; i < 16; ++i)
    {
        const int alpha = AlphaAt(pixels, i, i);
        partial = partial || (alpha > 0 && alpha < 255);
    }
    EXPECT_TRUE(partial);
}
