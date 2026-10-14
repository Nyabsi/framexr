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

#include "Compose.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <variant>
#include <vector>

#include "Device.hpp"
#include "FontCache.hpp"
#include "ScreenRenderer.hpp"
#include "Shaders.hpp"

namespace
{

constexpr size_t kResolutionDivider = 4;
constexpr auto kPressFlashDuration = std::chrono::milliseconds(180);

auto PressFlashColor() -> Eigen::Vector4f
{
    return {198.0f / 255.0f, 160.0f / 255.0f, 246.0f / 255.0f, 0.55f};
}

auto TruncateToIndex(float value) -> size_t
{
    return value <= 0.0f ? 0 : static_cast<size_t>(value);
}

} // namespace

auto Compose::Create(std::unique_ptr<Form> form, float scale) -> Result<std::unique_ptr<Compose>>
{
    auto compose = std::make_unique<Compose>();
    RETURN_IF_ERROR(compose->SetForm(std::move(form), scale));
    return compose;
}

Compose::Compose(VkExtent2D extent) : m_extent(extent)
{
}

Compose::Compose(std::unique_ptr<ScreenRenderer> screen) : m_screen(std::move(screen))
{
}

Compose::~Compose() = default;

auto Compose::SetForm(std::unique_ptr<Form> form, float scale) -> Result<>
{
    const auto width = static_cast<uint32_t>(std::lround(form->Size().x() * scale));
    const auto height = static_cast<uint32_t>(std::lround(form->Size().y() * scale));
    RETURN_IF_ERROR(Resize(width, height));
    m_form = std::move(form);
    m_scale = scale;
    m_hovered = {};
    m_pressed = {};
    m_released.clear();
    m_form->UpdateAll();
    Rebuild();
    return {};
}

auto Compose::Resize(uint32_t width, uint32_t height) -> Result<>
{
    if (m_foreground && width == m_extent.width && height == m_extent.height)
    {
        return {};
    }
    const VkFormat format = g_device->NativeFormat();
    if (!m_fill_pipeline)
    {
        ASSIGN_OR_RETURN(m_fill_pipeline, g_device->CreatePipeline(Shaders::Color(), format, Blend::AlphaMaxCoverage));
        ASSIGN_OR_RETURN(m_glyph_pipeline, g_device->CreatePipeline(Shaders::Glyph(), format, Blend::AlphaMaxCoverage));
        ASSIGN_OR_RETURN(m_sprite_pipeline, g_device->CreatePipeline(Shaders::SpriteTint(), format, Blend::AlphaMaxCoverage));
        ASSIGN_OR_RETURN(m_composite_pipeline, g_device->CreatePipeline(Shaders::Srgb(), format, Blend::AlphaMaxCoverage));
    }
    ASSIGN_OR_RETURN(m_foreground, g_device->CreateRenderTexture(width, height, format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    ASSIGN_OR_RETURN(m_background, g_device->CreateRenderTexture(width, height, format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    m_extent = {width, height};
    m_interact_stride = width / kResolutionDivider;
    m_interact_rows = height / kResolutionDivider;
    m_interact_map.assign(m_interact_stride * m_interact_rows, std::nullopt);
    return {};
}

auto Compose::Rebuild() -> void
{
    std::vector<const FormComponent*> owners(m_items.size(), nullptr);
    for (size_t index = 0; index < m_items.size(); ++index)
    {
        if (const auto* interact = std::get_if<InteractItem>(&m_items[index]))
        {
            owners[index] = interact->owner;
        }
    }
    m_items = m_form->Build(m_scale);
    const auto remap = [this, &owners](std::optional<size_t>& index) -> void
    {
        const auto* owner = index && *index < owners.size() ? owners[*index] : nullptr;
        index.reset();
        for (size_t moved = 0; owner != nullptr && moved < m_items.size(); ++moved)
        {
            if (const auto* interact = std::get_if<InteractItem>(&m_items[moved]); interact != nullptr && interact->owner == owner)
            {
                index = moved;
                return;
            }
        }
    };
    for (auto& index : m_hovered)
    {
        remap(index);
    }
    for (auto& index : m_pressed)
    {
        remap(index);
    }
    std::erase_if(
        m_released,
        [&remap](PressFlash& flash) -> bool
        {
            std::optional<size_t> index = flash.index;
            remap(index);
            flash.index = index.value_or(0);
            return !index;
        }
    );
    std::ranges::fill(m_interact_map, std::nullopt);
    for (size_t index = 0; index < m_items.size(); ++index)
    {
        if (const auto* interact = std::get_if<InteractItem>(&m_items[index]); interact != nullptr && interact->clickable)
        {
            MarkInteractive(interact->rect, index);
        }
    }
    m_background_dirty = true;
    m_foreground_dirty = true;
    m_highlight_dirty = true;
}

auto Compose::FindText(std::string_view text) const -> FormComponent*
{
    for (const auto& item : m_items)
    {
        if (const auto* label = std::get_if<TextItem>(&item); label != nullptr && label->text == text)
        {
            return label->owner;
        }
    }
    return nullptr;
}

auto Compose::MarkInteractive(const CanvasRect& rect, size_t index) -> void
{
    const auto x_min = TruncateToIndex(rect.x) / kResolutionDivider;
    const auto y_min = TruncateToIndex(rect.y) / kResolutionDivider;
    const auto x_max = std::min(x_min + TruncateToIndex(rect.w) / kResolutionDivider, m_interact_stride - 1);
    const auto y_max = std::min(y_min + TruncateToIndex(rect.h) / kResolutionDivider, m_interact_rows - 1);
    for (auto row = y_min; row < y_max; ++row)
    {
        for (auto column = x_min; column < x_max; ++column)
        {
            m_interact_map[row * m_interact_stride + column] = static_cast<uint16_t>(index);
        }
    }
}

auto Compose::InteractiveAt(const Eigen::Vector2f& uv) const -> std::optional<size_t>
{
    if (m_interact_map.empty())
    {
        return std::nullopt;
    }
    const auto x = std::min(
        TruncateToIndex(uv.x() * static_cast<float>(m_extent.width)) / kResolutionDivider,
        m_interact_stride - 1
    );
    const auto y = std::min(TruncateToIndex(uv.y() * static_cast<float>(m_extent.height)) / kResolutionDivider, m_interact_rows - 1);
    return m_interact_map[y * m_interact_stride + x];
}

auto Compose::Pause() -> Result<>
{
    return m_screen ? m_screen->Pause() : Result<>{};
}

auto Compose::Resume() -> Result<>
{
    return m_screen ? m_screen->Resume() : Result<>{};
}

auto Compose::ShouldRender() -> Result<RenderReadiness>
{
    if (m_screen)
    {
        return m_screen->ShouldRender();
    }
    if (!m_form)
    {
        return RenderReadiness::Unable;
    }
    if (m_form->UpdateAll())
    {
        Rebuild();
    }
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(m_released, [now](const PressFlash& flash) -> bool { return now - flash.released > kPressFlashDuration; });
    if (!m_released.empty())
    {
        m_highlight_dirty = true;
    }
    if (m_background_dirty || m_foreground_dirty || m_highlight_dirty)
    {
        return RenderReadiness::Should;
    }
    return RenderReadiness::Can;
}

auto Compose::Render(const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool>
{
    if (m_screen)
    {
        return m_screen->Render(target, batch, alpha);
    }
    if (!m_form)
    {
        return false;
    }
    ASSIGN_OR_RETURN(auto command_buffer, g_device->CreateCommandBuffer(g_device->GraphicsQueue()));
    RETURN_IF_ERROR(RenderForm(command_buffer, target, alpha));
    batch.Push(std::move(command_buffer));
    return true;
}

auto Compose::Meta() -> std::optional<FrameMeta>
{
    if (m_screen)
    {
        return m_screen->Meta();
    }
    if (m_extent.width == 0 || m_extent.height == 0)
    {
        return std::nullopt;
    }
    return FrameMeta{.extent = m_extent, .format = g_device->NativeFormat()};
}

auto Compose::SetTexelFootprint(const TexelFootprint& footprint) -> void
{
    if (m_screen)
    {
        m_screen->SetTexelFootprint(footprint);
    }
}

auto Compose::RenderRoundedRect(
    CommandBuffer& command_buffer,
    Pipeline& pipeline,
    const CanvasRect& rect,
    float corner_radius,
    const Eigen::Vector4f& color,
    const std::optional<HighlightOutline>& outline
) -> Result<>
{
    ASSIGN_OR_RETURN(auto vertices, g_device->UploadVertices({m_extent.width, m_extent.height}, rect.x, rect.y, rect.w, rect.h));
    const float radius = std::min({corner_radius, rect.w / 2.0f, rect.h / 2.0f});
    const Eigen::Vector4f outline_color = outline ? outline->color : Eigen::Vector4f::Zero();
    const float outline_width = outline ? outline->width : 0.0f;
    const std::array uniform{
        color.x(),
        color.y(),
        color.z(),
        color.w(),
        outline_color.x(),
        outline_color.y(),
        outline_color.z(),
        outline_color.w(),
        rect.w,
        rect.h,
        radius,
        outline_width,
    };
    ASSIGN_OR_RETURN(auto set, pipeline.CreateUniformSet(0, uniform));
    command_buffer.Draw(pipeline.CreatePass({m_extent.width, m_extent.height}, std::move(vertices), {std::move(set)}));
    return {};
}

auto Compose::PressStrength(size_t index, std::chrono::steady_clock::time_point now) const -> float
{
    if (std::ranges::find(m_pressed, std::optional<size_t>(index)) != m_pressed.end())
    {
        return 1.0f;
    }
    float strength = 0.0f;
    for (const auto& flash : m_released)
    {
        if (flash.index == index)
        {
            const std::chrono::duration<float> elapsed = now - flash.released;
            const std::chrono::duration<float> duration = kPressFlashDuration;
            strength = std::max(strength, 1.0f - (elapsed / duration));
        }
    }
    return std::clamp(strength, 0.0f, 1.0f);
}

auto Compose::RenderSprite(CommandBuffer& command_buffer, const SpriteItem& item) -> Result<>
{
    auto& pipeline = *m_sprite_pipeline;
    const auto& rect = item.rect;
    ASSIGN_OR_RETURN(auto vertices, g_device->UploadVertices({m_extent.width, m_extent.height}, rect.x, rect.y, rect.w, rect.h));
    ASSIGN_OR_RETURN(auto texture_set, pipeline.CreateTextureSet(0, item.image, g_device->TextureFilter()));
    const auto& st = item.st;
    const auto& color = item.tint;
    const std::array uniform{st.x(), st.y(), st.z(), st.w(), color.x(), color.y(), color.z(), color.w()};
    ASSIGN_OR_RETURN(auto uniform_set, pipeline.CreateUniformSet(1, uniform));
    command_buffer.Draw(
        pipeline.CreatePass({m_extent.width, m_extent.height}, std::move(vertices), {std::move(texture_set), std::move(uniform_set)})
    );
    return {};
}

auto Compose::RenderText(CommandBuffer& command_buffer, const TextItem& item) -> Result<>
{
    const auto& rect = item.rect;
    const auto size = static_cast<float>(item.font_size);
    const bool centered = item.centered;
    auto& pipeline = *m_glyph_pipeline;

    float cursor_y = rect.y;
    std::optional<TextSize> measured;
    if (centered)
    {
        ASSIGN_OR_RETURN(measured, g_fonts->MeasureText(item.text, item.font_size));
        cursor_y = std::fma(size, -0.25f, std::fma(measured->height, -0.5f, rect.y + rect.h));
    }

    for (const auto line : FontCache::SplitLines(item.text))
    {
        float cursor_x = centered ? std::fma(measured->width, -0.5f, std::fma(rect.w, 0.5f, rect.x)) : rect.x;
        ASSIGN_OR_RETURN(const auto glyphs, g_fonts->Glyphs(line, item.font_size));
        for (const auto& glyph : glyphs)
        {
            if (glyph->texture)
            {
                ASSIGN_OR_RETURN(
                    auto vertices,
                    g_device->UploadVertices(
                        {m_extent.width, m_extent.height},
                        cursor_x + glyph->left,
                        cursor_y - glyph->top,
                        glyph->width,
                        glyph->height
                    )
                );
                ASSIGN_OR_RETURN(auto texture_set, pipeline.CreateTextureSet(0, glyph->texture, g_device->TextureFilter()));
                const std::array color{item.color.x(), item.color.y(), item.color.z(), item.color.w()};
                ASSIGN_OR_RETURN(auto color_set, pipeline.CreateUniformSet(1, color));
                command_buffer.Draw(pipeline.CreatePass(
                    {m_extent.width, m_extent.height},
                    std::move(vertices),
                    {std::move(texture_set), std::move(color_set)}
                ));
            }
            cursor_x += glyph->advance;
        }
        cursor_y += size * 1.5f;
    }
    return {};
}

auto Compose::RenderForm(CommandBuffer& command_buffer, const std::shared_ptr<Image>& target, float alpha) -> Result<>
{
    m_highlight_dirty = false;

    if (m_background_dirty)
    {
        command_buffer.BeginRendering(m_background);
        for (const auto& item : m_items)
        {
            if (const auto* fill = std::get_if<FillItem>(&item))
            {
                RETURN_IF_ERROR(
                    RenderRoundedRect(command_buffer, *m_fill_pipeline, fill->rect, fill->radius, fill->color, std::nullopt)
                );
            }
            else if (const auto* sprite = std::get_if<SpriteItem>(&item))
            {
                RETURN_IF_ERROR(RenderSprite(command_buffer, *sprite));
            }
        }
        command_buffer.EndRendering();
        m_background_dirty = false;
    }

    if (m_foreground_dirty)
    {
        command_buffer.BeginRendering(m_foreground);
        for (const auto& item : m_items)
        {
            if (const auto* text = std::get_if<TextItem>(&item))
            {
                RETURN_IF_ERROR(RenderText(command_buffer, *text));
            }
        }
        command_buffer.EndRendering();
        m_foreground_dirty = false;
    }

    auto& composite = *m_composite_pipeline;
    ASSIGN_OR_RETURN(auto foreground_set, composite.CreateTextureSet(0, m_foreground, g_device->TextureFilter()));
    ASSIGN_OR_RETURN(auto background_set, composite.CreateTextureSet(0, m_background, g_device->TextureFilter()));
    const std::array alpha_uniform{alpha};
    ASSIGN_OR_RETURN(auto alpha_set, composite.CreateUniformSet(1, alpha_uniform));
    auto foreground_pass = composite.CreatePassForTarget(*target, {std::move(foreground_set), alpha_set});
    auto background_pass = composite.CreatePassForTarget(*target, {std::move(background_set), alpha_set});

    command_buffer.BeginRendering(target);
    command_buffer.Draw(background_pass);

    const auto now = std::chrono::steady_clock::now();
    for (size_t index = 0; index < m_items.size(); ++index)
    {
        const auto* interact = std::get_if<InteractItem>(&m_items[index]);
        if (interact == nullptr)
        {
            continue;
        }
        const auto& control = *interact->owner;
        const auto render_highlight = [&](const Eigen::Vector4f& color, const std::optional<HighlightOutline>& outline) -> Result<>
        { return RenderRoundedRect(command_buffer, *m_fill_pipeline, interact->rect, interact->radius, color, outline); };
        const auto color = control.HighlightColor();
        const auto outline = control.Outline();
        if (color || outline)
        {
            RETURN_IF_ERROR(render_highlight(color.value_or(Eigen::Vector4f::Zero()), outline));
        }
        if (const float strength = PressStrength(index, now); strength > 0.0f)
        {
            Eigen::Vector4f flash = PressFlashColor();
            flash.w() *= strength;
            RETURN_IF_ERROR(render_highlight(flash, std::nullopt));
        }
        if (std::ranges::find(m_hovered, std::optional<size_t>(index)) != m_hovered.end())
        {
            RETURN_IF_ERROR(render_highlight(Eigen::Vector4f(1.0f, 1.0f, 1.0f, 0.3f), std::nullopt));
        }
    }

    command_buffer.Draw(foreground_pass);
    command_buffer.EndRendering();
    return {};
}

auto Compose::OnHover(const PointerHit& hit) -> std::optional<Haptics>
{
    if (!m_form)
    {
        return std::nullopt;
    }
    m_highlight_dirty = true;
    const auto previous = m_hovered[hit.pointer];
    m_hovered[hit.pointer] = InteractiveAt(hit.uv);
    if (previous == m_hovered[hit.pointer])
    {
        return std::nullopt;
    }
    return Haptics{.intensity = 0.1f, .duration = 0.01f, .frequency = 5.0f};
}

auto Compose::OnLeft(size_t pointer) -> void
{
    m_highlight_dirty = true;
    m_hovered[pointer].reset();
}

auto Compose::OnPointer(const PointerHit& hit, bool pressed) -> void
{
    if (!m_form)
    {
        return;
    }
    const auto index = pressed ? InteractiveAt(hit.uv) : m_pressed[hit.pointer];
    if (!index)
    {
        return;
    }
    auto& control = *std::get<InteractItem>(m_items[*index]).owner;
    m_highlight_dirty = true;
    if (pressed)
    {
        m_pressed[hit.pointer] = index;
        control.OnPress(hit.mode);
    }
    else
    {
        m_pressed[hit.pointer].reset();
        m_released.push_back({.index = *index, .released = std::chrono::steady_clock::now()});
        control.OnRelease();
    }
}

auto Compose::OnScroll(const PointerHit& hit, float delta_y, float delta_x) -> void
{
    if (!m_form)
    {
        return;
    }
    if (const auto index = m_hovered[hit.pointer])
    {
        std::get<InteractItem>(m_items[*index]).owner->OnScroll(delta_y, delta_x);
    }
}
