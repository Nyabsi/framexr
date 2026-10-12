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

#include "Canvas.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

#include "CommandBatch.hpp"
#include "Device.hpp"
#include "Font.hpp"
#include "InteractionHandler.hpp"
#include "OverlayRenderer.hpp"
#include "Shaders.hpp"
#include "Theme.hpp"

namespace {

constexpr size_t kResolutionDivider = 4;
constexpr auto kPressFlashDuration = std::chrono::milliseconds(180);

auto PressFlashColor() -> Eigen::Vector4f {
    return {198.0f / 255.0f, 160.0f / 255.0f, 246.0f / 255.0f, 0.55f};
}

auto TruncateToIndex(float value) -> size_t {
    return value <= 0.0f ? 0 : static_cast<size_t>(value);
}

auto ParseHexByte(std::string_view text) -> std::optional<uint8_t> {
    uint8_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

auto Canvas::Create(uint32_t width, uint32_t height, VkFormat format) -> Result<std::unique_ptr<Canvas>> {
    ASSIGN_OR_RETURN(
        auto foreground,
        g_device->CreateRenderTexture(width, height, format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    );
    ASSIGN_OR_RETURN(
        auto background,
        g_device->CreateRenderTexture(width, height, format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    );

    CanvasPipelines pipelines;
    ASSIGN_OR_RETURN(pipelines.background_color, g_device->CreatePipeline(Shaders::Color(), format, Blend::AlphaMaxCoverage));
    ASSIGN_OR_RETURN(pipelines.foreground_glyph, g_device->CreatePipeline(Shaders::Glyph(), format, Blend::AlphaMaxCoverage));
    ASSIGN_OR_RETURN(pipelines.background_sprite, g_device->CreatePipeline(Shaders::SpriteTint(), format, Blend::AlphaMaxCoverage));
    ASSIGN_OR_RETURN(
        pipelines.highlight_color,
        g_device->CreatePipeline(Shaders::Color(), g_device->NativeFormat(), Blend::AlphaMaxCoverage)
    );
    ASSIGN_OR_RETURN(
        pipelines.highlight_sprite,
        g_device->CreatePipeline(Shaders::SpriteHighlight(), g_device->NativeFormat(), Blend::AlphaMaxCoverage)
    );
    ASSIGN_OR_RETURN(
        pipelines.composite,
        g_device->CreatePipeline(Shaders::Srgb(), g_device->NativeFormat(), Blend::AlphaMaxCoverage)
    );

    return std::unique_ptr<Canvas>(
        new Canvas(width, height, format, std::move(pipelines), std::move(foreground), std::move(background))
    );
}

Canvas::Canvas(
    uint32_t width,
    uint32_t height,
    VkFormat format,
    CanvasPipelines pipelines,
    std::shared_ptr<Image> foreground,
    std::shared_ptr<Image> background
)
    : m_width(width),
      m_height(height),
      m_format(format),
      m_pipelines(std::move(pipelines)),
      m_foreground(std::move(foreground)),
      m_background(std::move(background)),
      m_interact_map((width / kResolutionDivider) * (height / kResolutionDivider)),
      m_interact_stride(width / kResolutionDivider),
      m_interact_rows(height / kResolutionDivider) {
}

auto Canvas::ParseColor(std::string_view color) -> Result<Eigen::Vector4f> {
    const auto resolved = g_themes->Color(color);
    const std::string_view html_hex = resolved;
    if (html_hex.size() == 7 || html_hex.size() == 9) {
        const auto r = ParseHexByte(html_hex.substr(1, 2));
        const auto g = ParseHexByte(html_hex.substr(3, 2));
        const auto b = ParseHexByte(html_hex.substr(5, 2));
        const auto a = html_hex.size() == 9 ? ParseHexByte(html_hex.substr(7, 2)) : std::optional<uint8_t>(255);
        if (r && g && b && a) {
            return Eigen::Vector4f(
                static_cast<float>(*r) / 255.0f,
                static_cast<float>(*g) / 255.0f,
                static_cast<float>(*b) / 255.0f,
                html_hex.size() == 9 ? static_cast<float>(*a) / 255.0f : 1.0f
            );
        }
    }
    return Unexpected(std::format("Invalid color string: '{}', must be 7 characters long (e.g. #FF00FF)", html_hex));
}

auto Canvas::FallbackColor() -> Eigen::Vector4f {
    return {1.0f, 0.0f, 1.0f, 1.0f};
}

auto Canvas::Clear() -> void {
    m_controls.clear();
    std::ranges::fill(m_interact_map, std::nullopt);
    m_hovered = {};
    m_pressed = {};
    m_released.clear();
    m_background_dirty = true;
    m_foreground_dirty = true;
    m_highlight_dirty = true;
}

auto Canvas::AddControl(std::unique_ptr<Control> control) -> Control& {
    m_controls.push_back(control ? std::move(control) : std::make_unique<Control>());
    return *m_controls.back();
}

auto Canvas::MarkInteractive(float x, float y, float w, float h, size_t index) -> void {
    const auto x_min = TruncateToIndex(x) / kResolutionDivider;
    const auto y_min = TruncateToIndex(y) / kResolutionDivider;
    const auto x_max = std::min(x_min + TruncateToIndex(w) / kResolutionDivider, m_interact_stride - 1);
    const auto y_max = std::min(y_min + TruncateToIndex(h) / kResolutionDivider, m_interact_rows - 1);
    for (auto row = y_min; row < y_max; ++row) {
        for (auto column = x_min; column < x_max; ++column) {
            m_interact_map[row * m_interact_stride + column] = static_cast<uint16_t>(index);
        }
    }
}

auto Canvas::InteractiveAt(const Eigen::Vector2f& uv) const -> std::optional<size_t> {
    const auto x = std::min(TruncateToIndex(uv.x() * static_cast<float>(m_width)) / kResolutionDivider, m_interact_stride - 1);
    const auto y = std::min(TruncateToIndex(uv.y() * static_cast<float>(m_height)) / kResolutionDivider, m_interact_rows - 1);
    return m_interact_map[y * m_interact_stride + x];
}

auto Canvas::Panel(float x, float y, float w, float h, float radius) -> Control& {
    auto& control = AddControl(nullptr);
    control.SetRect({x, y, w, h});
    control.SetCornerRadius(radius);
    control.SetBgColor(BgColor());
    control.SetBackground(BackgroundStyle::RoundedRect);
    return control;
}

auto Canvas::Label(float x, float y, float w, float h, float radius, std::string_view text, std::unique_ptr<Control> control)
    -> Control& {
    auto& label = AddControl(std::move(control));
    label.SetRect({x, y, w, h});
    label.SetCornerRadius(radius);
    label.SetText(text);
    label.SetFgColor(FgColor());
    label.SetFontSize(FontSize());
    label.SetForeground(ForegroundStyle::Text);
    return label;
}

auto Canvas::LabelCentered(float x, float y, float w, float h, float radius, std::string_view text, std::unique_ptr<Control> control)
    -> Control& {
    auto& label = Label(x, y, w, h, radius, text, std::move(control));
    label.SetForeground(ForegroundStyle::TextCentered);
    return label;
}

auto Canvas::Sprite(float x, float y, float w, float h) -> Control& {
    auto& sprite = AddControl(nullptr);
    sprite.SetRect({x, y, w, h});
    sprite.SetBackground(BackgroundStyle::Sprite);
    return sprite;
}

auto Canvas::SpriteInteractive(float x, float y, float w, float h) -> Control& {
    auto& sprite = Sprite(x, y, w, h);
    sprite.SetHighlight(HighlightStyle::Sprite);
    return sprite;
}

auto Canvas::Button(float x, float y, float w, float h, float radius, std::string_view text, std::unique_ptr<Control> control)
    -> Control& {
    MarkInteractive(x, y, w, h, m_controls.size());
    auto& button = AddControl(std::move(control));
    button.SetRect({x, y, w, h});
    button.SetCornerRadius(radius);
    button.SetText(text);
    button.SetFgColor(FgColor());
    button.SetBgColor(BgColor());
    button.SetFontSize(FontSize());
    button.SetBackground(BackgroundStyle::RoundedRect);
    button.SetForeground(ForegroundStyle::TextCentered);
    button.SetHighlight(HighlightStyle::RoundedRect);
    return button;
}

auto Canvas::Decoration(float x, float y, float w, float h, float radius, std::unique_ptr<Control> control) -> Control& {
    auto& decoration = AddControl(std::move(control));
    decoration.SetRect({x, y, w, h});
    decoration.SetCornerRadius(radius);
    decoration.SetHighlight(HighlightStyle::RoundedRect);
    return decoration;
}

auto Canvas::KeyButton(
    float x,
    float y,
    float w,
    float h,
    float radius,
    KeyCapType cap_type,
    std::span<const std::string> labels,
    std::unique_ptr<Control> control
) -> Control& {
    const auto index = m_controls.size();
    MarkInteractive(x, y, w, h, index);

    auto& key = AddControl(std::move(control));
    key.SetRect({x, y, w, h});
    key.SetCornerRadius(radius);
    key.SetBgColor(BgColor());
    key.SetBackground(BackgroundStyle::RoundedRect);
    key.SetHighlight(HighlightStyle::RoundedRect);

    const auto size = static_cast<float>(FontSize());
    std::vector<KeyCapLabel> layout;
    switch (cap_type) {
    case KeyCapType::Regular: layout.push_back({ForegroundStyle::TextCentered, {x, y, w, h - size}, 1.0f}); break;
    case KeyCapType::RegularAltGr:
        layout.push_back({ForegroundStyle::Text, {x + 12.0f, y + size + 12.0f, w, h}, 1.0f});
        layout.push_back({ForegroundStyle::Text, {std::fma(w, 0.5f, x) + 12.0f, y + h - size + 8.0f, w, h}, 0.8f});
        break;
    case KeyCapType::Reversed:
        layout.push_back({ForegroundStyle::TextCentered, {x, std::fma(h, 0.5f, y) + 2.0f, w, h * 0.5f}, 1.0f});
        layout.push_back({ForegroundStyle::TextCentered, {x, y + 2.0f, w, h * 0.5f}, 0.8f});
        break;
    case KeyCapType::ReversedAltGr:
        layout.push_back({ForegroundStyle::Text, {x + 12.0f, y + h - size + 4.0f, w, h}, 1.0f});
        layout.push_back({ForegroundStyle::Text, {x + 12.0f, y + size + 8.0f, w, h}, 0.8f});
        layout.push_back({ForegroundStyle::Text, {std::fma(w, 0.5f, x) + 8.0f, y + h - size + 4.0f, w, h}, 0.8f});
        break;
    }

    for (size_t i = 0; i < layout.size() && i < labels.size(); ++i) {
        auto& label = AddControl(nullptr);
        label.SetRect(layout[i].rect);
        label.SetText(labels[i]);
        label.SetFgColor(FgColor() * layout[i].alpha);
        label.SetFontSize(FontSize());
        label.SetForeground(layout[i].style);
    }
    return *m_controls[index];
}

auto Canvas::SetRenderer(std::unique_ptr<OverlayRenderer>) -> void {
}

auto Canvas::SetInteraction(std::unique_ptr<InteractionHandler>) -> void {
}

auto Canvas::Init() -> Result<> {
    return {};
}

auto Canvas::Pause() -> Result<> {
    return {};
}

auto Canvas::Resume() -> Result<> {
    return {};
}

auto Canvas::ShouldRender() -> Result<RenderReadiness> {
    for (auto& control : m_controls) {
        control->Update();
        if (control->ForegroundDirty()) {
            m_foreground_dirty = true;
            control->SetForegroundDirty(false);
        }
        if (control->BackgroundDirty()) {
            m_background_dirty = true;
            control->SetBackgroundDirty(false);
        }
    }
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(m_released, [now](const PressFlash& flash) -> bool { return now - flash.released > kPressFlashDuration; });
    if (!m_released.empty()) {
        m_highlight_dirty = true;
    }
    if (m_background_dirty || m_foreground_dirty || m_highlight_dirty) {
        return RenderReadiness::Should;
    }
    return RenderReadiness::Can;
}

auto Canvas::RenderRoundedRect(
    CommandBuffer& command_buffer,
    Pipeline& pipeline,
    const Control& control,
    const Eigen::Vector4f& color,
    const std::optional<HighlightOutline>& outline
) -> Result<> {
    const auto& rect = control.Rect();
    ASSIGN_OR_RETURN(auto vertices, g_device->UploadVertices({m_width, m_height}, rect.x, rect.y, rect.w, rect.h));
    const float radius = std::min({control.CornerRadius(), rect.w / 2.0f, rect.h / 2.0f});
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
    command_buffer.Draw(pipeline.CreatePass({m_width, m_height}, std::move(vertices), {std::move(set)}));
    return {};
}

auto Canvas::PressStrength(size_t index, std::chrono::steady_clock::time_point now) const -> float {
    if (std::ranges::find(m_pressed, std::optional<size_t>(index)) != m_pressed.end()) {
        return 1.0f;
    }
    float strength = 0.0f;
    for (const auto& flash : m_released) {
        if (flash.index == index) {
            const std::chrono::duration<float> elapsed = now - flash.released;
            const std::chrono::duration<float> duration = kPressFlashDuration;
            strength = std::max(strength, 1.0f - (elapsed / duration));
        }
    }
    return std::clamp(strength, 0.0f, 1.0f);
}

auto Canvas::RenderSprite(CommandBuffer& command_buffer, Pipeline& pipeline, const Control& control, const Eigen::Vector4f& color)
    -> Result<> {
    if (!control.Sprite()) {
        return {};
    }
    const auto& rect = control.Rect();
    ASSIGN_OR_RETURN(auto vertices, g_device->UploadVertices({m_width, m_height}, rect.x, rect.y, rect.w, rect.h));
    ASSIGN_OR_RETURN(auto texture_set, pipeline.CreateTextureSet(0, control.Sprite(), g_device->TextureFilter()));
    const auto& st = control.SpriteSt();
    const std::array uniform{st.x(), st.y(), st.z(), st.w(), color.x(), color.y(), color.z(), color.w()};
    ASSIGN_OR_RETURN(auto uniform_set, pipeline.CreateUniformSet(1, uniform));
    command_buffer.Draw(
        pipeline.CreatePass({m_width, m_height}, std::move(vertices), {std::move(texture_set), std::move(uniform_set)})
    );
    return {};
}

auto Canvas::RenderText(CommandBuffer& command_buffer, const Control& control, bool centered) -> Result<> {
    const auto& rect = control.Rect();
    const auto size = static_cast<float>(control.FontSize());
    auto& pipeline = *m_pipelines.foreground_glyph;

    float cursor_y = rect.y;
    std::optional<TextSize> measured;
    if (centered) {
        ASSIGN_OR_RETURN(measured, g_fonts->MeasureText(control.Text(), control.FontSize()));
        cursor_y = std::fma(size, -0.25f, std::fma(measured->height, -0.5f, rect.y + rect.h));
    }

    for (const auto line : FontCache::SplitLines(control.Text())) {
        float cursor_x = centered ? std::fma(measured->width, -0.5f, std::fma(rect.w, 0.5f, rect.x)) : rect.x;
        ASSIGN_OR_RETURN(const auto glyphs, g_fonts->Glyphs(line, control.FontSize()));
        for (const auto& glyph : glyphs) {
            if (glyph->texture) {
                ASSIGN_OR_RETURN(
                    auto vertices,
                    g_device->UploadVertices(
                        {m_width, m_height},
                        cursor_x + glyph->left,
                        cursor_y - glyph->top,
                        glyph->width,
                        glyph->height
                    )
                );
                ASSIGN_OR_RETURN(auto texture_set, pipeline.CreateTextureSet(0, glyph->texture, g_device->TextureFilter()));
                const std::array color{control.FgColor().x(), control.FgColor().y(), control.FgColor().z(), control.FgColor().w()};
                ASSIGN_OR_RETURN(auto color_set, pipeline.CreateUniformSet(1, color));
                command_buffer.Draw(
                    pipeline.CreatePass({m_width, m_height}, std::move(vertices), {std::move(texture_set), std::move(color_set)})
                );
            }
            cursor_x += glyph->advance;
        }
        cursor_y += size * 1.5f;
    }
    return {};
}

auto Canvas::Render(const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> {
    m_highlight_dirty = false;
    ASSIGN_OR_RETURN(auto command_buffer, g_device->CreateCommandBuffer(g_device->GraphicsQueue()));

    if (m_background_dirty) {
        command_buffer.BeginRendering(m_background);
        for (const auto& control : m_controls) {
            if (control->Background() == BackgroundStyle::RoundedRect) {
                RETURN_IF_ERROR(
                    RenderRoundedRect(command_buffer, *m_pipelines.background_color, *control, control->BgColor(), std::nullopt)
                );
            } else if (control->Background() == BackgroundStyle::Sprite) {
                RETURN_IF_ERROR(RenderSprite(command_buffer, *m_pipelines.background_sprite, *control, control->FgColor()));
            }
        }
        command_buffer.EndRendering();
        m_background_dirty = false;
    }

    if (m_foreground_dirty) {
        command_buffer.BeginRendering(m_foreground);
        for (const auto& control : m_controls) {
            if (control->Foreground() != ForegroundStyle::None) {
                RETURN_IF_ERROR(RenderText(command_buffer, *control, control->Foreground() == ForegroundStyle::TextCentered));
            }
        }
        command_buffer.EndRendering();
        m_foreground_dirty = false;
    }

    auto& composite = *m_pipelines.composite;
    ASSIGN_OR_RETURN(auto foreground_set, composite.CreateTextureSet(0, m_foreground, g_device->TextureFilter()));
    ASSIGN_OR_RETURN(auto background_set, composite.CreateTextureSet(0, m_background, g_device->TextureFilter()));
    const std::array alpha_uniform{alpha};
    ASSIGN_OR_RETURN(auto alpha_set, composite.CreateUniformSet(1, alpha_uniform));
    auto foreground_pass = composite.CreatePassForTarget(*target, {std::move(foreground_set), alpha_set});
    auto background_pass = composite.CreatePassForTarget(*target, {std::move(background_set), alpha_set});

    command_buffer.BeginRendering(target);
    command_buffer.Draw(background_pass);

    const auto now = std::chrono::steady_clock::now();
    for (size_t index = 0; index < m_controls.size(); ++index) {
        auto& control = *m_controls[index];
        if (control.Highlight() == HighlightStyle::None) {
            continue;
        }
        const auto render_highlight = [&](const Eigen::Vector4f& color,
                                          const std::optional<HighlightOutline>& outline) -> Result<> {
            if (control.Highlight() == HighlightStyle::Sprite) {
                return RenderSprite(command_buffer, *m_pipelines.highlight_sprite, control, color);
            }
            return RenderRoundedRect(command_buffer, *m_pipelines.highlight_color, control, color, outline);
        };
        const auto color = control.HighlightColor();
        const auto outline = control.Outline();
        if (color || outline) {
            RETURN_IF_ERROR(render_highlight(color.value_or(Eigen::Vector4f::Zero()), outline));
        }
        if (const float strength = PressStrength(index, now); strength > 0.0f) {
            Eigen::Vector4f flash = PressFlashColor();
            flash.w() *= strength;
            RETURN_IF_ERROR(render_highlight(flash, std::nullopt));
        }
        if (std::ranges::find(m_hovered, std::optional<size_t>(index)) != m_hovered.end()) {
            RETURN_IF_ERROR(render_highlight(Eigen::Vector4f(1.0f, 1.0f, 1.0f, 0.3f), std::nullopt));
        }
    }

    command_buffer.Draw(foreground_pass);
    command_buffer.EndRendering();
    batch.Push(std::move(command_buffer));
    return true;
}

auto Canvas::Meta() -> std::optional<FrameMeta> {
    return FrameMeta{.extent = {m_width, m_height}, .format = m_format};
}

auto Canvas::OnHover(const PointerHit& hit) -> std::optional<Haptics> {
    m_highlight_dirty = true;
    const auto previous = m_hovered[hit.pointer];
    m_hovered[hit.pointer] = InteractiveAt(hit.uv);
    if (previous == m_hovered[hit.pointer]) {
        return std::nullopt;
    }
    return Haptics{.intensity = 0.1f, .duration = 0.01f, .frequency = 5.0f};
}

auto Canvas::OnLeft(size_t pointer) -> void {
    m_highlight_dirty = true;
    m_hovered[pointer].reset();
}

auto Canvas::OnPointer(const PointerHit& hit, bool pressed) -> void {
    const auto index = pressed ? InteractiveAt(hit.uv) : m_pressed[hit.pointer];
    if (!index) {
        return;
    }
    auto& control = *m_controls[*index];
    m_highlight_dirty = true;
    if (pressed) {
        m_pressed[hit.pointer] = index;
        control.OnPress(hit.mode);
    } else {
        m_pressed[hit.pointer].reset();
        m_released.push_back({.index = *index, .released = std::chrono::steady_clock::now()});
        control.OnRelease();
    }
}

auto Canvas::OnScroll(const PointerHit& hit, float delta_y, float delta_x) -> void {
    if (const auto index = m_hovered[hit.pointer]) {
        m_controls[*index]->OnScroll(delta_y, delta_x);
    }
}
