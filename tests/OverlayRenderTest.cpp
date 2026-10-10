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

#include <gtest/gtest.h>

#include "Anchor.hpp"
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

auto RenderOverlay(Overlay& overlay, const std::string& name) -> void {
    auto& app = TestApp::Get();
    ASSERT_TRUE(overlay.backend->Init(app).has_value());
    const auto readiness = overlay.backend->ShouldRender(app);
    ASSERT_TRUE(readiness.has_value());
    EXPECT_EQ(*readiness, RenderReadiness::Should);
    const auto meta = overlay.backend->Meta();
    ASSERT_TRUE(meta.has_value());

    auto target = app.device->CreateRenderTexture(
        meta->extent.width,
        meta->extent.height,
        app.device->NativeFormat(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    );
    ASSERT_TRUE(target.has_value());
    CommandBatch batch;
    const auto rendered = overlay.backend->Render(app, *target, batch, 1.0f);
    ASSERT_TRUE(rendered.has_value());
    EXPECT_TRUE(*rendered);
    ASSERT_TRUE(batch.Submit().has_value());
    app.device->GraphicsQueue().WaitIdle();

    const auto pixels = GpuReadback::Read(*app.device, **target);
    ASSERT_FALSE(pixels.empty());
    EXPECT_GT(GpuReadback::DistinctColors(pixels), 2U);
    Png::Write(TestApp::OutputDir() / std::format("{}.png", name), meta->extent.width, meta->extent.height, pixels);
}

} // namespace

TEST(OverlayRender, Watch) {
    auto watch = Watch::Create(TestApp::Get());
    ASSERT_TRUE(watch.has_value());
    EXPECT_TRUE((*watch)->state.want_visible);
    RenderOverlay(**watch, "watch");
}

TEST(OverlayRender, Anchor) {
    auto anchor = Anchor::Create(TestApp::Get());
    ASSERT_TRUE(anchor.has_value());
    RenderOverlay(**anchor, "anchor");
}

TEST(OverlayRender, KeyboardWithSessionKeymap) {
    std::optional<Keymap> keymap;
    if (auto loaded = Keymap::FromWayland()) {
        keymap.emplace(std::move(*loaded));
    }
    auto keyboard = Keyboard::Create(TestApp::Get(), std::move(keymap));
    ASSERT_TRUE(keyboard.has_value());
    RenderOverlay(**keyboard, "keyboard");
}

TEST(OverlayRender, SettingsPanel) {
    auto settings = CustomUi::Create(TestApp::Get(), "settings");
    ASSERT_TRUE(settings.has_value());
    Overlay overlay(std::move(settings->first), std::move(settings->second));
    RenderOverlay(overlay, "settings");
}
