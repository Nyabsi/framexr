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

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Core>
#include <vulkan/vulkan.h>

#include "Control.hpp"
#include "Device.hpp"
#include "Error.hpp"
#include "Overlay.hpp"
#include "Pipeline.hpp"

enum class KeyCapType : uint8_t {
    Regular,
    RegularAltGr,
    Reversed,
    ReversedAltGr,
};

struct KeyCapLabel {
    ForegroundStyle style = ForegroundStyle::None;
    CanvasRect rect;
    float alpha = 1.0f;
};

struct CanvasPipelines {
    std::shared_ptr<Pipeline> background_color;
    std::shared_ptr<Pipeline> background_sprite;
    std::shared_ptr<Pipeline> foreground_glyph;
    std::shared_ptr<Pipeline> highlight_color;
    std::shared_ptr<Pipeline> highlight_sprite;
    std::shared_ptr<Pipeline> composite;
};

class Canvas : public OverlayBackend {
  public:
    static auto Create(std::shared_ptr<Device> device, uint32_t width, uint32_t height, VkFormat format)
        -> Result<std::unique_ptr<Canvas>>;
    static auto ParseColor(std::string_view html_hex) -> Result<Eigen::Vector4f>;
    static auto FallbackColor() -> Eigen::Vector4f;

    Eigen::Vector4f fg_color = Eigen::Vector4f::Ones();
    Eigen::Vector4f bg_color = Eigen::Vector4f::Zero();
    int font_size = 16;

    [[nodiscard]] auto Width() const -> uint32_t { return width_; }
    [[nodiscard]] auto Height() const -> uint32_t { return height_; }

    auto Panel(float x, float y, float w, float h, float radius) -> Control&;
    auto Label(float x, float y, float w, float h, float radius, std::string text, std::unique_ptr<Control> control = nullptr)
        -> Control&;
    auto LabelCentered(float x, float y, float w, float h, float radius, std::string text, std::unique_ptr<Control> control = nullptr)
        -> Control&;
    auto Sprite(float x, float y, float w, float h) -> Control&;
    auto SpriteInteractive(float x, float y, float w, float h) -> Control&;
    auto Button(float x, float y, float w, float h, float radius, std::string text, std::unique_ptr<Control> control = nullptr)
        -> Control&;
    auto KeyButton(
        float x,
        float y,
        float w,
        float h,
        float radius,
        KeyCapType cap_type,
        std::span<const std::string> labels,
        std::unique_ptr<Control> control
    ) -> Control&;

    auto SetRenderer(std::unique_ptr<OverlayRenderer> renderer) -> void override;
    auto SetInteraction(std::unique_ptr<InteractionHandler> interaction) -> void override;

    auto Init(AppState& app) -> Result<> override;
    auto Pause(AppState& app) -> Result<> override;
    auto Resume(AppState& app) -> Result<> override;
    auto ShouldRender(AppState& app) -> Result<RenderReadiness> override;
    auto Render(AppState& app, const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> override;
    auto Meta() -> std::optional<FrameMeta> override;

    auto OnHover(AppState& app, const PointerHit& hit) -> std::optional<Haptics> override;
    auto OnLeft(AppState& app, size_t pointer) -> void override;
    auto OnPointer(AppState& app, const PointerHit& hit, bool pressed) -> void override;
    auto OnScroll(AppState& app, const PointerHit& hit, float delta_y, float delta_x) -> void override;

  protected:
    Canvas(
        std::shared_ptr<Device> device,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        CanvasPipelines pipelines,
        std::shared_ptr<Image> foreground,
        std::shared_ptr<Image> background
    );

  private:
    auto AddControl(std::unique_ptr<Control> control) -> Control&;
    auto MarkInteractive(float x, float y, float w, float h, size_t index) -> void;
    [[nodiscard]] auto InteractiveAt(const Eigen::Vector2f& uv) const -> std::optional<size_t>;

    auto RenderRoundedRect(CommandBuffer& command_buffer, Pipeline& pipeline, const Control& control, const Eigen::Vector4f& color)
        -> Result<>;
    auto RenderSprite(CommandBuffer& command_buffer, Pipeline& pipeline, const Control& control, const Eigen::Vector4f& color)
        -> Result<>;
    auto RenderText(AppState& app, CommandBuffer& command_buffer, const Control& control, bool centered) -> Result<>;

    std::shared_ptr<Device> device_;
    uint32_t width_;
    uint32_t height_;
    VkFormat format_;
    CanvasPipelines pipelines_;
    std::shared_ptr<Image> foreground_;
    std::shared_ptr<Image> background_;
    std::vector<std::unique_ptr<Control>> controls_;
    std::array<std::optional<size_t>, 2> hovered_;
    std::array<std::optional<size_t>, 2> pressed_;
    std::vector<std::optional<uint16_t>> interact_map_;
    size_t interact_stride_;
    size_t interact_rows_;
    bool background_dirty_ = false;
    bool foreground_dirty_ = false;
    bool highlight_dirty_ = false;
};
