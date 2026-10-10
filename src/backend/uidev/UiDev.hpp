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

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>
#include <xcb/xcb.h>

#include "Canvas.hpp"
#include "Error.hpp"

class AppState;
class Device;
class Image;

enum class WindowEvent : uint8_t {
    None,
    Close,
    Resized,
};

class PreviewWindow final {
  public:
    static auto Create(std::string_view title) -> Result<std::unique_ptr<PreviewWindow>>;

    ~PreviewWindow();
    PreviewWindow(const PreviewWindow&) = delete;
    auto operator=(const PreviewWindow&) -> PreviewWindow& = delete;

    [[nodiscard]] auto Connection() const -> xcb_connection_t* { return connection_; }
    [[nodiscard]] auto Window() const -> xcb_window_t { return window_; }
    [[nodiscard]] auto Extent() const -> VkExtent2D { return extent_; }

    auto SetFixedSize(uint32_t width, uint32_t height) -> void;
    auto Poll() -> WindowEvent;

  private:
    PreviewWindow(xcb_connection_t* connection, xcb_window_t window, xcb_atom_t delete_atom);

    xcb_connection_t* connection_;
    xcb_window_t window_;
    xcb_atom_t delete_atom_;
    VkExtent2D extent_{1, 1};
};

struct PreviewSwapchain {
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    std::vector<std::shared_ptr<Image>> images;
};

class UiDev final {
  public:
    static auto Run(const std::string& panel_name) -> Result<>;

  private:
    UiDev(std::shared_ptr<Device> device, std::unique_ptr<PreviewWindow> window, VkSurfaceKHR surface, std::string panel_name);

    auto Rebuild(AppState& app) -> Result<>;
    auto CreateSwapchain() -> Result<>;
    auto DestroySwapchain() -> void;
    auto Draw(AppState& app) -> Result<bool>;
    auto Loop(AppState& app) -> Result<>;
    auto Destroy() -> void;

    static auto AddDummyDevices(AppState& app) -> void;
    static auto AddDummyScreens(AppState& app) -> void;

    std::shared_ptr<Device> device_;
    std::unique_ptr<PreviewWindow> window_;
    VkSurfaceKHR surface_;
    std::string panel_name_;
    std::filesystem::path watch_path_;
    PreviewSwapchain swapchain_;
    std::unique_ptr<Canvas> canvas_;
    VkExtent2D last_size_{};
};
