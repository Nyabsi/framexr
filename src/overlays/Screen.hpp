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

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <vulkan/vulkan.h>

#include "Capture.hpp"
#include "Error.hpp"
#include "Frame.hpp"
#include "Overlay.hpp"
#include "ScreenCastPortal.hpp"
#include "State.hpp"

class Device;
class Image;
class Pipeline;
class Queue;
class WaylandClient;
struct WaylandOutput;
struct Config;

class CapturedFrame final : public ProcessedFrame {
  public:
    CapturedFrame(std::shared_ptr<Image> captured_image, FrameFormat frame_format, std::optional<MouseMeta> mouse_meta);

    std::shared_ptr<Image> image;
    FrameFormat format;
    std::optional<MouseMeta> mouse;
};

class PendingFrame final : public ProcessedFrame {
  public:
    explicit PendingFrame(Frame&& frame);
    ~PendingFrame() override;

    PendingFrame(const PendingFrame&) = delete;
    auto operator=(const PendingFrame&) -> PendingFrame& = delete;

    Frame frame;
    std::vector<std::byte> bytes;
};

class FrameUploader final {
  public:
    FrameUploader(std::string name, std::shared_ptr<Device> device, Queue& queue);

    auto Process(Frame&& frame) const -> std::unique_ptr<CapturedFrame>;

  private:
    [[nodiscard]] auto Upload(const FrameFormat& format, std::span<const std::byte> data, int32_t stride) const
        -> std::shared_ptr<Image>;

    std::string name_;
    std::shared_ptr<Device> device_;
    Queue* queue_;
};

class ScreenPipeline final {
  public:
    static auto Create(AppState& app, VkExtent2D extent) -> Result<ScreenPipeline>;

    auto Render(AppState& app, const CapturedFrame& frame, const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha)
        -> Result<>;

  private:
    ScreenPipeline(std::shared_ptr<Pipeline> pipeline, std::shared_ptr<Image> mouse, VkExtent2D extent);

    std::shared_ptr<Pipeline> pipeline_;
    std::shared_ptr<Image> mouse_;
    VkExtent2D extent_;
};

class ScreenRenderer final : public OverlayRenderer {
  public:
    ScreenRenderer(std::string name, std::unique_ptr<Capture> capture);

    auto Init(AppState& app) -> Result<> override;
    auto Pause(AppState& app) -> Result<> override;
    auto Resume(AppState& app) -> Result<> override;
    auto ShouldRender(AppState& app) -> Result<RenderReadiness> override;
    auto Render(AppState& app, const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> override;
    auto Meta() -> std::optional<FrameMeta> override;

  private:
    auto Start(AppState& app) -> void;

    std::string name_;
    std::unique_ptr<Capture> capture_;
    std::shared_ptr<FrameUploader> uploader_;
    bool main_thread_ = false;
    std::optional<ScreenPipeline> pipeline_;
    std::unique_ptr<CapturedFrame> current_;
    std::optional<FrameMeta> meta_;
};

class ScreenInteraction final : public InteractionHandler {
  public:
    ScreenInteraction(const Eigen::Vector2f& position, const Eigen::Vector2f& size, FrameTransform transform);

    auto OnHover(AppState& app, const PointerHit& hit) -> std::optional<Haptics> override;
    auto OnLeft(AppState& app, size_t pointer) -> void override;
    auto OnPointer(AppState& app, const PointerHit& hit, bool pressed) -> void override;
    auto OnScroll(AppState& app, const PointerHit& hit, float delta_y, float delta_x) -> void override;

  private:
    Eigen::Affine2f mouse_transform_;
};

struct ScreenOverlay {
    ScreenMeta meta;
    OverlayState state;
    std::unique_ptr<OverlayBackend> backend;
};

class Screens final {
  public:
    static auto CreateWayland(WaylandClient& client, AppState& app) -> std::vector<ScreenOverlay>;
    static auto CreateWaylandRenderer(
        const WaylandOutput& output,
        bool has_wlr_dmabuf,
        bool has_wlr_screencopy,
        std::map<std::string, std::string>& tokens,
        const AppState& app
    ) -> std::unique_ptr<ScreenRenderer>;
    static auto CreateX11Pipewire(AppState& app) -> Result<std::vector<ScreenOverlay>>;
    static auto CreateXshm(AppState& app) -> Result<std::vector<ScreenOverlay>>;
    static auto SelectScreen(std::string_view instructions, ScreenCastOptions options) -> Result<ScreenSelection>;
    static auto LoadTokens() -> Result<std::map<std::string, std::string>>;
    static auto SaveTokens(const std::map<std::string, std::string>& tokens) -> Result<>;

  private:
    static auto CreateState(std::string name, const Eigen::Vector2i& resolution, FrameTransform transform, const Config& config)
        -> OverlayState;
};
