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
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Core>
#include <vulkan/vulkan.h>

#include "CommandBatch.hpp"
#include "Control.hpp"
#include "Device.hpp"
#include "Error.hpp"
#include "InteractionHandler.hpp"
#include "Overlay.hpp"
#include "OverlayRenderer.hpp"
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

struct PressFlash {
    size_t index = 0;
    std::chrono::steady_clock::time_point released;
};

class Canvas : public OverlayBackend {
  public:
    static auto Create(uint32_t width, uint32_t height, VkFormat format) -> Result<std::unique_ptr<Canvas>>;
    static auto ParseColor(std::string_view color) -> Result<Eigen::Vector4f>;
    static auto FallbackColor() -> Eigen::Vector4f;

    [[nodiscard]] auto FgColor() const -> const Eigen::Vector4f& { return m_fg_color; }
    [[nodiscard]] auto FgColor() -> Eigen::Vector4f& { return m_fg_color; }
    [[nodiscard]] auto BgColor() const -> const Eigen::Vector4f& { return m_bg_color; }
    [[nodiscard]] auto BgColor() -> Eigen::Vector4f& { return m_bg_color; }
    [[nodiscard]] auto FontSize() const -> int { return m_font_size; }

    auto SetFgColor(Eigen::Vector4f fg_color) -> void { m_fg_color = std::move(fg_color); }
    auto SetBgColor(Eigen::Vector4f bg_color) -> void { m_bg_color = std::move(bg_color); }
    auto SetFontSize(int font_size) -> void { m_font_size = font_size; }

    [[nodiscard]] auto Width() const -> uint32_t { return m_width; }
    [[nodiscard]] auto Height() const -> uint32_t { return m_height; }
    [[nodiscard]] auto ControlCount() const -> size_t { return m_controls.size(); }
    [[nodiscard]] auto ControlAt(size_t index) -> Control& { return *m_controls[index]; }

    auto Clear() -> void;
    auto MarkHighlightDirty() -> void { m_highlight_dirty = true; }

    auto Panel(float x, float y, float w, float h, float radius) -> Control&;
    auto Label(float x, float y, float w, float h, float radius, std::string_view text, std::unique_ptr<Control> control = nullptr)
        -> Control&;
    auto LabelCentered(
        float x,
        float y,
        float w,
        float h,
        float radius,
        std::string_view text,
        std::unique_ptr<Control> control = nullptr
    ) -> Control&;
    auto Sprite(float x, float y, float w, float h) -> Control&;
    auto SpriteInteractive(float x, float y, float w, float h) -> Control&;
    auto Button(float x, float y, float w, float h, float radius, std::string_view text, std::unique_ptr<Control> control = nullptr)
        -> Control&;
    auto Decoration(float x, float y, float w, float h, float radius, std::unique_ptr<Control> control) -> Control&;
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

    auto Init() -> Result<> override;
    auto Pause() -> Result<> override;
    auto Resume() -> Result<> override;
    auto ShouldRender() -> Result<RenderReadiness> override;
    auto Render(const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> override;
    auto Meta() -> std::optional<FrameMeta> override;

    auto OnHover(const PointerHit& hit) -> std::optional<Haptics> override;
    auto OnLeft(size_t pointer) -> void override;
    auto OnPointer(const PointerHit& hit, bool pressed) -> void override;
    auto OnScroll(const PointerHit& hit, float delta_y, float delta_x) -> void override;

  protected:
    Canvas(
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

    auto RenderRoundedRect(
        CommandBuffer& command_buffer,
        Pipeline& pipeline,
        const Control& control,
        const Eigen::Vector4f& color,
        const std::optional<HighlightOutline>& outline
    ) -> Result<>;
    [[nodiscard]] auto PressStrength(size_t index, std::chrono::steady_clock::time_point now) const -> float;
    auto RenderSprite(CommandBuffer& command_buffer, Pipeline& pipeline, const Control& control, const Eigen::Vector4f& color)
        -> Result<>;
    auto RenderText(CommandBuffer& command_buffer, const Control& control, bool centered) -> Result<>;

    uint32_t m_width;
    uint32_t m_height;
    VkFormat m_format;
    CanvasPipelines m_pipelines;
    std::shared_ptr<Image> m_foreground;
    std::shared_ptr<Image> m_background;
    std::vector<std::unique_ptr<Control>> m_controls;
    std::array<std::optional<size_t>, 2> m_hovered;
    std::array<std::optional<size_t>, 2> m_pressed;
    std::vector<PressFlash> m_released;
    std::vector<std::optional<uint16_t>> m_interact_map;
    size_t m_interact_stride;
    size_t m_interact_rows;
    bool m_background_dirty = false;
    bool m_foreground_dirty = false;
    bool m_highlight_dirty = false;

    Eigen::Vector4f m_fg_color = Eigen::Vector4f::Ones();
    Eigen::Vector4f m_bg_color = Eigen::Vector4f::Zero();
    int m_font_size = 16;
};
