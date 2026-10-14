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
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <Eigen/Geometry>
#include <vulkan/vulkan.h>

#include "CommandBatch.hpp"
#include "CommandBuffer.hpp"
#include "Error.hpp"
#include "Form.hpp"
#include "FormComponent.hpp"
#include "Image.hpp"
#include "Pipeline.hpp"
#include "methods/LaserPointer.hpp"

class ScreenRenderer;

enum class RenderReadiness : uint8_t
{
    Should,
    Can,
    Unable,
};

struct TexelFootprint
{
    Eigen::Vector3f eye = Eigen::Vector3f::UnitZ();
    Eigen::Vector2f size = Eigen::Vector2f::Ones();
    Eigen::Vector2f display_pixels_per_radian = Eigen::Vector2f::Zero();
};

struct FrameMeta
{
    VkExtent2D extent{};
    Eigen::Affine3f transform = Eigen::Affine3f::Identity();
    VkFormat format = VK_FORMAT_UNDEFINED;
};

struct PressFlash
{
    size_t index = 0;
    std::chrono::steady_clock::time_point released;
};

class Compose final
{
  public:
    static auto Create(std::unique_ptr<Form> form, float scale = 1.0f) -> Result<std::unique_ptr<Compose>>;

    explicit Compose(VkExtent2D extent = {});
    explicit Compose(std::unique_ptr<ScreenRenderer> screen);
    ~Compose();
    Compose(const Compose&) = delete;
    auto operator=(const Compose&) -> Compose& = delete;

    [[nodiscard]] auto Root() const -> Form* { return m_form.get(); }
    [[nodiscard]] auto Items() const -> const FormState& { return m_items; }
    [[nodiscard]] auto InteractiveAt(const Eigen::Vector2f& uv) const -> std::optional<size_t>;
    [[nodiscard]] auto FindText(std::string_view text) const -> FormComponent*;

    auto SetForm(std::unique_ptr<Form> form, float scale = 1.0f) -> Result<>;

    auto Pause() -> Result<>;
    auto Resume() -> Result<>;
    auto ShouldRender() -> Result<RenderReadiness>;
    auto Render(const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool>;
    auto Meta() -> std::optional<FrameMeta>;
    auto SetTexelFootprint(const TexelFootprint& footprint) -> void;

    auto OnHover(const PointerHit& hit) -> std::optional<Haptics>;
    auto OnLeft(size_t pointer) -> void;
    auto OnPointer(const PointerHit& hit, bool pressed) -> void;
    auto OnScroll(const PointerHit& hit, float delta_y, float delta_x) -> void;

  private:
    auto Resize(uint32_t width, uint32_t height) -> Result<>;
    auto Rebuild() -> void;
    auto MarkInteractive(const CanvasRect& rect, size_t index) -> void;
    [[nodiscard]] auto PressStrength(size_t index, std::chrono::steady_clock::time_point now) const -> float;
    auto RenderForm(CommandBuffer& command_buffer, const std::shared_ptr<Image>& target, float alpha) -> Result<>;
    auto RenderRoundedRect(
        CommandBuffer& command_buffer,
        Pipeline& pipeline,
        const CanvasRect& rect,
        float radius,
        const Eigen::Vector4f& color,
        const std::optional<HighlightOutline>& outline
    ) -> Result<>;
    auto RenderSprite(CommandBuffer& command_buffer, const SpriteItem& item) -> Result<>;
    auto RenderText(CommandBuffer& command_buffer, const TextItem& item) -> Result<>;

    VkExtent2D m_extent{};
    std::unique_ptr<ScreenRenderer> m_screen;
    std::unique_ptr<Form> m_form;
    float m_scale = 1.0f;
    FormState m_items;
    std::shared_ptr<Pipeline> m_fill_pipeline;
    std::shared_ptr<Pipeline> m_sprite_pipeline;
    std::shared_ptr<Pipeline> m_glyph_pipeline;
    std::shared_ptr<Pipeline> m_composite_pipeline;
    std::shared_ptr<Image> m_foreground;
    std::shared_ptr<Image> m_background;
    std::array<std::optional<size_t>, 2> m_hovered;
    std::array<std::optional<size_t>, 2> m_pressed;
    std::vector<PressFlash> m_released;
    std::vector<std::optional<uint16_t>> m_interact_map;
    size_t m_interact_stride = 0;
    size_t m_interact_rows = 0;
    bool m_background_dirty = false;
    bool m_foreground_dirty = false;
    bool m_highlight_dirty = false;
};
