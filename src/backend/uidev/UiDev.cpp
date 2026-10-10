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

#include "UiDev.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>
#include <thread>

#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan_xcb.h>

#include "CommandBuffer.hpp"
#include "Config.hpp"
#include "CustomUi.hpp"
#include "Device.hpp"
#include "Hid.hpp"
#include "Image.hpp"
#include "Logger.hpp"
#include "Modular.hpp"
#include "State.hpp"

namespace {

constexpr uint32_t kSizeHintProgramSize = 8;
constexpr uint32_t kSizeHintMinSize = 16;
constexpr uint32_t kSizeHintMaxSize = 32;
constexpr auto kRedrawInterval = std::chrono::milliseconds(100);
constexpr auto kPollInterval = std::chrono::milliseconds(5);

auto InternAtom(xcb_connection_t* connection, std::string_view name) -> xcb_atom_t {
    const auto cookie = xcb_intern_atom(connection, 0, static_cast<uint16_t>(name.size()), name.data());
    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(connection, cookie, nullptr);
    if (reply == nullptr) {
        return XCB_ATOM_NONE;
    }
    const xcb_atom_t atom = reply->atom;
    std::free(reply);
    return atom;
}

auto FormatName(VkFormat format) -> std::string {
    switch (format) {
    case VK_FORMAT_B8G8R8A8_SRGB: return "B8G8R8A8_SRGB";
    case VK_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB: return "R8G8B8A8_SRGB";
    case VK_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32: return "A8B8G8R8_SRGB_PACK32";
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32: return "A8B8G8R8_UNORM_PACK32";
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32: return "A2R10G10B10_UNORM_PACK32";
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "A2B10G10R10_UNORM_PACK32";
    case VK_FORMAT_R16G16B16A16_SFLOAT: return "R16G16B16A16_SFLOAT";
    default: return std::format("{}", static_cast<int>(format));
    }
}

auto VkError(std::string_view what, VkResult result) -> Error {
    return Error(std::format("{} failed: {}", what, static_cast<int>(result)));
}

} // namespace

PreviewWindow::PreviewWindow(xcb_connection_t* connection, xcb_window_t window, xcb_atom_t delete_atom)
    : connection_(connection),
      window_(window),
      delete_atom_(delete_atom) {
}

PreviewWindow::~PreviewWindow() {
    xcb_destroy_window(connection_, window_);
    xcb_disconnect(connection_);
}

auto PreviewWindow::Create(std::string_view title) -> Result<std::unique_ptr<PreviewWindow>> {
    xcb_connection_t* connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(connection) != 0) {
        xcb_disconnect(connection);
        return Unexpected("Failed to connect to the X server");
    }
    const xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    const xcb_window_t window = xcb_generate_id(connection);
    const std::array<uint32_t, 1> values{XCB_EVENT_MASK_STRUCTURE_NOTIFY};
    xcb_create_window(
        connection,
        XCB_COPY_FROM_PARENT,
        window,
        screen->root,
        0,
        0,
        1,
        1,
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        screen->root_visual,
        XCB_CW_EVENT_MASK,
        values.data()
    );
    xcb_change_property(
        connection,
        XCB_PROP_MODE_REPLACE,
        window,
        XCB_ATOM_WM_NAME,
        XCB_ATOM_STRING,
        8,
        static_cast<uint32_t>(title.size()),
        title.data()
    );

    const xcb_atom_t protocols = InternAtom(connection, "WM_PROTOCOLS");
    const xcb_atom_t delete_atom = InternAtom(connection, "WM_DELETE_WINDOW");
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, protocols, XCB_ATOM_ATOM, 32, 1, &delete_atom);

    xcb_map_window(connection, window);
    xcb_flush(connection);
    return std::unique_ptr<PreviewWindow>(new PreviewWindow(connection, window, delete_atom));
}

auto PreviewWindow::SetFixedSize(uint32_t width, uint32_t height) -> void {
    std::array<uint32_t, 18> hints{};
    hints[0] = kSizeHintProgramSize | kSizeHintMinSize | kSizeHintMaxSize;
    hints[3] = width;
    hints[4] = height;
    hints[5] = width;
    hints[6] = height;
    hints[7] = width;
    hints[8] = height;
    xcb_change_property(
        connection_,
        XCB_PROP_MODE_REPLACE,
        window_,
        XCB_ATOM_WM_NORMAL_HINTS,
        XCB_ATOM_WM_SIZE_HINTS,
        32,
        static_cast<uint32_t>(hints.size()),
        hints.data()
    );
    const std::array<uint32_t, 2> size{width, height};
    xcb_configure_window(connection_, window_, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, size.data());
    xcb_flush(connection_);
    extent_ = {width, height};
}

auto PreviewWindow::Poll() -> WindowEvent {
    auto result = WindowEvent::None;
    while (xcb_generic_event_t* event = xcb_poll_for_event(connection_)) {
        switch (event->response_type & 0x7fU) {
        case XCB_CONFIGURE_NOTIFY: {
            const auto* configure = reinterpret_cast<const xcb_configure_notify_event_t*>(event);
            if (configure->width != extent_.width || configure->height != extent_.height) {
                extent_ = {configure->width, configure->height};
                if (result == WindowEvent::None) {
                    result = WindowEvent::Resized;
                }
            }
            break;
        }
        case XCB_CLIENT_MESSAGE: {
            const auto* message = reinterpret_cast<const xcb_client_message_event_t*>(event);
            if (message->data.data32[0] == delete_atom_) {
                result = WindowEvent::Close;
            }
            break;
        }
        default: break;
        }
        std::free(event);
    }
    return result;
}

UiDev::UiDev(std::shared_ptr<Device> device, std::unique_ptr<PreviewWindow> window, VkSurfaceKHR surface, std::string panel_name)
    : device_(std::move(device)),
      window_(std::move(window)),
      surface_(surface),
      panel_name_(std::move(panel_name)),
      watch_path_(Config::Root() / std::format("{}.yaml", panel_name_)) {
}

auto UiDev::Run(const std::string& panel_name) -> Result<> {
    DeviceCreateInfo device_info;
    device_info.instance_extensions = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XCB_SURFACE_EXTENSION_NAME};
    device_info.required_device_extensions = [](VkPhysicalDevice) -> std::vector<std::string> {
        return {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    };
    ASSIGN_OR_RETURN(auto device, Device::Create(device_info));
    ASSIGN_OR_RETURN(auto window, PreviewWindow::Create("FrameXR UI Preview"));

    const VkXcbSurfaceCreateInfoKHR surface_info{
        .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
        .connection = window->Connection(),
        .window = window->Window(),
    };
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (const auto result = vkCreateXcbSurfaceKHR(device->Instance(), &surface_info, nullptr, &surface); result != VK_SUCCESS) {
        return std::unexpected(VkError("vkCreateXcbSurfaceKHR", result));
    }

    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device->PhysicalDevice(), device->GraphicsQueue().Family(), surface, &supported);
    if (supported == VK_FALSE) {
        vkDestroySurfaceKHR(device->Instance(), surface, nullptr);
        return Unexpected("Graphics queue cannot present to the preview window");
    }

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device->PhysicalDevice(), surface, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device->PhysicalDevice(), surface, &format_count, formats.data());
    if (formats.empty()) {
        vkDestroySurfaceKHR(device->Instance(), surface, nullptr);
        return Unexpected("Preview window surface reports no formats");
    }
    device->SetNativeFormat(formats.front().format);
    LOG_INFO("Using surface format: {}", FormatName(formats.front().format));

    HidProvider::use_uinput.store(false, std::memory_order_relaxed);

    UiDev preview(device, std::move(window), surface, panel_name);
    auto app = AppState::Create(device);
    if (!app) {
        preview.Destroy();
        return std::unexpected(app.error());
    }
    AddDummyDevices(**app);
    AddDummyScreens(**app);

    auto result = preview.Loop(**app);
    preview.Destroy();
    return result;
}

auto UiDev::Loop(AppState& app) -> Result<> {
    RETURN_IF_ERROR(Rebuild(app));

    std::error_code error;
    auto last_modified = std::filesystem::last_write_time(watch_path_, error);
    if (error) {
        return Unexpected(std::format("{}: {}", watch_path_.string(), error.message()));
    }

    bool recreate = false;
    auto last_draw = std::chrono::steady_clock::time_point{};
    while (true) {
        const auto event = window_->Poll();
        if (event == WindowEvent::Close) {
            return {};
        }
        if (event == WindowEvent::Resized) {
            recreate = true;
        }

        if (std::chrono::steady_clock::now() - last_draw < kRedrawInterval && !recreate) {
            std::this_thread::sleep_for(kPollInterval);
            continue;
        }

        const auto modified = std::filesystem::last_write_time(watch_path_, error);
        if (error) {
            return Unexpected(std::format("{}: {}", watch_path_.string(), error.message()));
        }
        if (modified > last_modified) {
            recreate = true;
            last_modified = modified;
        }

        if (recreate) {
            RETURN_IF_ERROR(Rebuild(app));
            recreate = false;
        }

        ASSIGN_OR_RETURN(const bool presented, Draw(app));
        recreate = !presented;
        last_draw = std::chrono::steady_clock::now();
    }
}

auto UiDev::Rebuild(AppState& app) -> Result<> {
    canvas_.reset();
    ASSIGN_OR_RETURN(const auto config, CustomUi::Load(panel_name_));

    if (last_size_.width != config.size[0] || last_size_.height != config.size[1]) {
        window_->SetFixedSize(config.size[0], config.size[1]);
        last_size_ = {config.size[0], config.size[1]};
    }

    RETURN_IF_ERROR(CreateSwapchain());
    ASSIGN_OR_RETURN(canvas_, Modular::CreateCanvas(config.size, config.elements, app));
    return canvas_->Init(app);
}

auto UiDev::CreateSwapchain() -> Result<> {
    DestroySwapchain();

    VkSurfaceCapabilitiesKHR capabilities{.currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR};
    if (const auto result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device_->PhysicalDevice(), surface_, &capabilities);
        result != VK_SUCCESS) {
        return std::unexpected(VkError("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result));
    }

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent = window_->Extent();
    }

    VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (const auto bit :
         {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
          VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
          VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
          VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
        if ((capabilities.supportedCompositeAlpha & static_cast<VkCompositeAlphaFlagsKHR>(bit)) != 0) {
            composite_alpha = bit;
            break;
        }
    }

    const VkSwapchainCreateInfoKHR create_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface_,
        .minImageCount = std::max(capabilities.minImageCount, 2U),
        .imageFormat = device_->NativeFormat(),
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = composite_alpha,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };
    if (const auto result = vkCreateSwapchainKHR(device_->Handle(), &create_info, nullptr, &swapchain_.handle);
        result != VK_SUCCESS) {
        return std::unexpected(VkError("vkCreateSwapchainKHR", result));
    }

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(device_->Handle(), swapchain_.handle, &count, nullptr);
    std::vector<VkImage> images(count);
    vkGetSwapchainImagesKHR(device_->Handle(), swapchain_.handle, &count, images.data());
    for (VkImage image : images) {
        ASSIGN_OR_RETURN(auto wrapped, device_->WrapImage(image, device_->NativeFormat(), extent, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR));
        swapchain_.images.push_back(std::move(wrapped));
    }
    return {};
}

auto UiDev::DestroySwapchain() -> void {
    if (swapchain_.handle == VK_NULL_HANDLE) {
        return;
    }
    device_->GraphicsQueue().WaitIdle();
    device_->CollectFinished();
    swapchain_.images.clear();
    vkDestroySwapchainKHR(device_->Handle(), swapchain_.handle, nullptr);
    swapchain_.handle = VK_NULL_HANDLE;
}

auto UiDev::Draw(AppState& app) -> Result<bool> {
    const VkFenceCreateInfo fence_info{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    if (const auto result = vkCreateFence(device_->Handle(), &fence_info, nullptr, &fence); result != VK_SUCCESS) {
        return std::unexpected(VkError("vkCreateFence", result));
    }

    uint32_t index = 0;
    const auto acquired = vkAcquireNextImageKHR(device_->Handle(), swapchain_.handle, UINT64_MAX, VK_NULL_HANDLE, fence, &index);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        vkDestroyFence(device_->Handle(), fence, nullptr);
        return false;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        vkDestroyFence(device_->Handle(), fence, nullptr);
        FatalError(std::format("failed to acquire next image: {}", static_cast<int>(acquired)));
    }
    vkWaitForFences(device_->Handle(), 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device_->Handle(), fence, nullptr);

    CommandBatch batch;
    if (auto readiness = canvas_->ShouldRender(app); !readiness) {
        LOG_ERROR("failed to render canvas: {}", readiness.error().Message());
    } else if (auto rendered = canvas_->Render(app, swapchain_.images[index], batch, 1.0f); !rendered) {
        LOG_ERROR("failed to render canvas: {}", rendered.error().Message());
    }
    RETURN_IF_ERROR(batch.Submit());
    device_->GraphicsQueue().WaitIdle();
    device_->CollectFinished();

    const VkQueue queue = device_->GraphicsQueue().Handle();
    const VkPresentInfoKHR present_info{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .swapchainCount = 1,
        .pSwapchains = &swapchain_.handle,
        .pImageIndices = &index,
    };
    const auto presented = vkQueuePresentKHR(queue, &present_info);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        return false;
    }
    if (presented != VK_SUCCESS) {
        return std::unexpected(VkError("vkQueuePresentKHR", presented));
    }
    return acquired == VK_SUCCESS;
}

auto UiDev::Destroy() -> void {
    canvas_.reset();
    DestroySwapchain();
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(device_->Instance(), surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
}

auto UiDev::AddDummyDevices(AppState& app) -> void {
    auto& devices = app.input.devices;
    devices.push_back({.soc = 0.42f, .charging = true, .role = TrackedDeviceRole::Hmd});
    devices.push_back({.soc = 0.72f, .charging = false, .role = TrackedDeviceRole::LeftHand});
    devices.push_back({.soc = 0.73f, .charging = false, .role = TrackedDeviceRole::RightHand});
    devices.push_back({.soc = 0.65f, .charging = false, .role = TrackedDeviceRole::Tracker});
    devices.push_back({.soc = 0.67f, .charging = false, .role = TrackedDeviceRole::Tracker});
    devices.push_back({.soc = 0.69f, .charging = false, .role = TrackedDeviceRole::Tracker});
}

auto UiDev::AddDummyScreens(AppState& app) -> void {
    for (const auto* name : {"HDMI-A-1", "DP-2", "DP-3"}) {
        app.screens.push_back({.name = name, .id = OverlayId{0}, .native_handle = 0});
    }
}
