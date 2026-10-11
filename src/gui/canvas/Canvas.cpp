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

#include "Font.hpp"
#include "Shaders.hpp"
#include "Theme.hpp"
#include "State.hpp"

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

auto Canvas::Create(std::shared_ptr<Device> device, uint32_t width, uint32_t height, VkFormat format)
    -> Result<std::unique_ptr<Canvas>> {
    ASSIGN_OR_RETURN(auto foreground, device->CreateRenderTexture(width, height, format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    ASSIGN_OR_RETURN(auto background, device->CreateRenderTexture(width, height, format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

    CanvasPipelines pipelines;
    ASSIGN_OR_RETURN(pipelines.background_color, device->CreatePipeline(Shaders::Color(), format, Blend::AlphaMaxCoverage));
    ASSIGN_OR_RETURN(pipelines.foreground_glyph, device->CreatePipeline(Shaders::Glyph(), format, Blend::AlphaMaxCoverage));
    ASSIGN_OR_RETURN(pipelines.background_sprite, device->CreatePipeline(Shaders::SpriteTint(), format, Blend::AlphaMaxCoverage));
    ASSIGN_OR_RETURN(
        pipelines.highlight_color,
        device->CreatePipeline(Shaders::Color(), device->NativeFormat(), Blend::AlphaMaxCoverage)
    );
    ASSIGN_OR_RETURN(
        pipelines.highlight_sprite,
        device->CreatePipeline(Shaders::SpriteHighlight(), device->NativeFormat(), Blend::AlphaMaxCoverage)
    );
    ASSIGN_OR_RETURN(pipelines.composite, device->CreatePipeline(Shaders::Srgb(), device->NativeFormat(), Blend::AlphaMaxCoverage));

    return std::unique_ptr<Canvas>(
        new Canvas(std::move(device), width, height, format, std::move(pipelines), std::move(foreground), std::move(background))
    );
}

Canvas::Canvas(
    std::shared_ptr<Device> device,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    CanvasPipelines pipelines,
    std::shared_ptr<Image> foreground,
    std::shared_ptr<Image> background
)
    : device_(std::move(device)),
      width_(width),
      height_(height),
      format_(format),
      pipelines_(std::move(pipelines)),
      foreground_(std::move(foreground)),
      background_(std::move(background)),
      interact_map_((width / kResolutionDivider) * (height / kResolutionDivider)),
      interact_stride_(width / kResolutionDivider),
      interact_rows_(height / kResolutionDivider) {
}

auto Canvas::ParseColor(std::string_view color) -> Result<Eigen::Vector4f> {
    const auto resolved = Theme::Color(color);
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

auto Canvas::AddControl(std::unique_ptr<Control> control) -> Control& {
    controls_.push_back(control ? std::move(control) : std::make_unique<Control>());
    return *controls_.back();
}

auto Canvas::MarkInteractive(float x, float y, float w, float h, size_t index) -> void {
    const auto x_min = TruncateToIndex(x) / kResolutionDivider;
    const auto y_min = TruncateToIndex(y) / kResolutionDivider;
    const auto x_max = std::min(x_min + TruncateToIndex(w) / kResolutionDivider, interact_stride_ - 1);
    const auto y_max = std::min(y_min + TruncateToIndex(h) / kResolutionDivider, interact_rows_ - 1);
    for (auto row = y_min; row < y_max; ++row) {
        for (auto column = x_min; column < x_max; ++column) {
            interact_map_[row * interact_stride_ + column] = static_cast<uint16_t>(index);
        }
    }
}

auto Canvas::InteractiveAt(const Eigen::Vector2f& uv) const -> std::optional<size_t> {
    const auto x = std::min(TruncateToIndex(uv.x() * static_cast<float>(width_)) / kResolutionDivider, interact_stride_ - 1);
    const auto y = std::min(TruncateToIndex(uv.y() * static_cast<float>(height_)) / kResolutionDivider, interact_rows_ - 1);
    return interact_map_[y * interact_stride_ + x];
}

auto Canvas::Panel(float x, float y, float w, float h, float radius) -> Control& {
    auto& control = AddControl(nullptr);
    control.rect = {x, y, w, h};
    control.corner_radius = radius;
    control.bg_color = bg_color;
    control.background = BackgroundStyle::RoundedRect;
    return control;
}

auto Canvas::Label(float x, float y, float w, float h, float radius, std::string text, std::unique_ptr<Control> control)
    -> Control& {
    auto& label = AddControl(std::move(control));
    label.rect = {x, y, w, h};
    label.corner_radius = radius;
    label.text = std::move(text);
    label.fg_color = fg_color;
    label.font_size = font_size;
    label.foreground = ForegroundStyle::Text;
    return label;
}

auto Canvas::LabelCentered(float x, float y, float w, float h, float radius, std::string text, std::unique_ptr<Control> control)
    -> Control& {
    auto& label = Label(x, y, w, h, radius, std::move(text), std::move(control));
    label.foreground = ForegroundStyle::TextCentered;
    return label;
}

auto Canvas::Sprite(float x, float y, float w, float h) -> Control& {
    auto& sprite = AddControl(nullptr);
    sprite.rect = {x, y, w, h};
    sprite.background = BackgroundStyle::Sprite;
    return sprite;
}

auto Canvas::SpriteInteractive(float x, float y, float w, float h) -> Control& {
    auto& sprite = Sprite(x, y, w, h);
    sprite.highlight = HighlightStyle::Sprite;
    return sprite;
}

auto Canvas::Button(float x, float y, float w, float h, float radius, std::string text, std::unique_ptr<Control> control)
    -> Control& {
    MarkInteractive(x, y, w, h, controls_.size());
    auto& button = AddControl(std::move(control));
    button.rect = {x, y, w, h};
    button.corner_radius = radius;
    button.text = std::move(text);
    button.fg_color = fg_color;
    button.bg_color = bg_color;
    button.font_size = font_size;
    button.background = BackgroundStyle::RoundedRect;
    button.foreground = ForegroundStyle::TextCentered;
    button.highlight = HighlightStyle::RoundedRect;
    return button;
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
    const auto index = controls_.size();
    MarkInteractive(x, y, w, h, index);

    auto& key = AddControl(std::move(control));
    key.rect = {x, y, w, h};
    key.corner_radius = radius;
    key.bg_color = bg_color;
    key.background = BackgroundStyle::RoundedRect;
    key.highlight = HighlightStyle::RoundedRect;

    const auto size = static_cast<float>(font_size);
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
        label.rect = layout[i].rect;
        label.text = labels[i];
        label.fg_color = fg_color * layout[i].alpha;
        label.font_size = font_size;
        label.foreground = layout[i].style;
    }
    return *controls_[index];
}

auto Canvas::SetRenderer(std::unique_ptr<OverlayRenderer>) -> void {
}

auto Canvas::SetInteraction(std::unique_ptr<InteractionHandler>) -> void {
}

auto Canvas::Init(AppState&) -> Result<> {
    return {};
}

auto Canvas::Pause(AppState&) -> Result<> {
    return {};
}

auto Canvas::Resume(AppState&) -> Result<> {
    return {};
}

auto Canvas::ShouldRender(AppState& app) -> Result<RenderReadiness> {
    for (auto& control : controls_) {
        control->Update(app);
        if (control->foreground_dirty) {
            foreground_dirty_ = true;
            control->foreground_dirty = false;
        }
        if (control->background_dirty) {
            background_dirty_ = true;
            control->background_dirty = false;
        }
    }
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(released_, [now](const PressFlash& flash) -> bool { return now - flash.released > kPressFlashDuration; });
    if (!released_.empty()) {
        highlight_dirty_ = true;
    }
    if (background_dirty_ || foreground_dirty_ || highlight_dirty_) {
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
    const auto& rect = control.rect;
    ASSIGN_OR_RETURN(auto vertices, device_->UploadVertices({width_, height_}, rect.x, rect.y, rect.w, rect.h));
    const float radius = std::min({control.corner_radius, rect.w / 2.0f, rect.h / 2.0f});
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
    command_buffer.Draw(pipeline.CreatePass({width_, height_}, std::move(vertices), {std::move(set)}));
    return {};
}

auto Canvas::PressStrength(size_t index, std::chrono::steady_clock::time_point now) const -> float {
    if (std::ranges::find(pressed_, std::optional<size_t>(index)) != pressed_.end()) {
        return 1.0f;
    }
    float strength = 0.0f;
    for (const auto& flash : released_) {
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
    if (!control.sprite) {
        return {};
    }
    const auto& rect = control.rect;
    ASSIGN_OR_RETURN(auto vertices, device_->UploadVertices({width_, height_}, rect.x, rect.y, rect.w, rect.h));
    ASSIGN_OR_RETURN(auto texture_set, pipeline.CreateTextureSet(0, control.sprite, device_->TextureFilter()));
    const auto& st = control.sprite_st;
    const std::array uniform{st.x(), st.y(), st.z(), st.w(), color.x(), color.y(), color.z(), color.w()};
    ASSIGN_OR_RETURN(auto uniform_set, pipeline.CreateUniformSet(1, uniform));
    command_buffer.Draw(
        pipeline.CreatePass({width_, height_}, std::move(vertices), {std::move(texture_set), std::move(uniform_set)})
    );
    return {};
}

auto Canvas::RenderText(AppState& app, CommandBuffer& command_buffer, const Control& control, bool centered) -> Result<> {
    const auto& rect = control.rect;
    const auto size = static_cast<float>(control.font_size);
    auto& pipeline = *pipelines_.foreground_glyph;

    float cursor_y = rect.y;
    std::optional<TextSize> measured;
    if (centered) {
        ASSIGN_OR_RETURN(measured, app.fonts->MeasureText(control.text, control.font_size, *device_));
        cursor_y = std::fma(size, -0.25f, std::fma(measured->height, -0.5f, rect.y + rect.h));
    }

    for (const auto line : FontCache::SplitLines(control.text)) {
        float cursor_x = centered ? std::fma(measured->width, -0.5f, std::fma(rect.w, 0.5f, rect.x)) : rect.x;
        ASSIGN_OR_RETURN(const auto glyphs, app.fonts->Glyphs(line, control.font_size, *device_));
        for (const auto& glyph : glyphs) {
            if (glyph->texture) {
                ASSIGN_OR_RETURN(
                    auto vertices,
                    device_->UploadVertices(
                        {width_, height_},
                        cursor_x + glyph->left,
                        cursor_y - glyph->top,
                        glyph->width,
                        glyph->height
                    )
                );
                ASSIGN_OR_RETURN(auto texture_set, pipeline.CreateTextureSet(0, glyph->texture, device_->TextureFilter()));
                const std::array color{control.fg_color.x(), control.fg_color.y(), control.fg_color.z(), control.fg_color.w()};
                ASSIGN_OR_RETURN(auto color_set, pipeline.CreateUniformSet(1, color));
                command_buffer.Draw(
                    pipeline.CreatePass({width_, height_}, std::move(vertices), {std::move(texture_set), std::move(color_set)})
                );
            }
            cursor_x += glyph->advance;
        }
        cursor_y += size * 1.5f;
    }
    return {};
}

auto Canvas::Render(AppState& app, const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> {
    highlight_dirty_ = false;
    ASSIGN_OR_RETURN(auto command_buffer, device_->CreateCommandBuffer(device_->GraphicsQueue()));

    if (background_dirty_) {
        command_buffer.BeginRendering(background_);
        for (const auto& control : controls_) {
            if (control->background == BackgroundStyle::RoundedRect) {
                RETURN_IF_ERROR(
                    RenderRoundedRect(command_buffer, *pipelines_.background_color, *control, control->bg_color, std::nullopt)
                );
            } else if (control->background == BackgroundStyle::Sprite) {
                RETURN_IF_ERROR(RenderSprite(command_buffer, *pipelines_.background_sprite, *control, control->fg_color));
            }
        }
        command_buffer.EndRendering();
        background_dirty_ = false;
    }

    if (foreground_dirty_) {
        command_buffer.BeginRendering(foreground_);
        for (const auto& control : controls_) {
            if (control->foreground != ForegroundStyle::None) {
                RETURN_IF_ERROR(RenderText(app, command_buffer, *control, control->foreground == ForegroundStyle::TextCentered));
            }
        }
        command_buffer.EndRendering();
        foreground_dirty_ = false;
    }

    auto& composite = *pipelines_.composite;
    ASSIGN_OR_RETURN(auto foreground_set, composite.CreateTextureSet(0, foreground_, app.device->TextureFilter()));
    ASSIGN_OR_RETURN(auto background_set, composite.CreateTextureSet(0, background_, app.device->TextureFilter()));
    const std::array alpha_uniform{alpha};
    ASSIGN_OR_RETURN(auto alpha_set, composite.CreateUniformSet(1, alpha_uniform));
    auto foreground_pass = composite.CreatePassForTarget(*target, {std::move(foreground_set), alpha_set});
    auto background_pass = composite.CreatePassForTarget(*target, {std::move(background_set), alpha_set});

    command_buffer.BeginRendering(target);
    command_buffer.Draw(background_pass);

    const auto now = std::chrono::steady_clock::now();
    for (size_t index = 0; index < controls_.size(); ++index) {
        auto& control = *controls_[index];
        if (control.highlight == HighlightStyle::None) {
            continue;
        }
        const auto render_highlight = [&](const Eigen::Vector4f& color,
                                          const std::optional<HighlightOutline>& outline) -> Result<> {
            if (control.highlight == HighlightStyle::Sprite) {
                return RenderSprite(command_buffer, *pipelines_.highlight_sprite, control, color);
            }
            return RenderRoundedRect(command_buffer, *pipelines_.highlight_color, control, color, outline);
        };
        const auto color = control.HighlightColor(app);
        const auto outline = control.Outline(app);
        if (color || outline) {
            RETURN_IF_ERROR(render_highlight(color.value_or(Eigen::Vector4f::Zero()), outline));
        }
        if (const float strength = PressStrength(index, now); strength > 0.0f) {
            Eigen::Vector4f flash = PressFlashColor();
            flash.w() *= strength;
            RETURN_IF_ERROR(render_highlight(flash, std::nullopt));
        }
        if (std::ranges::find(hovered_, std::optional<size_t>(index)) != hovered_.end()) {
            RETURN_IF_ERROR(render_highlight(Eigen::Vector4f(1.0f, 1.0f, 1.0f, 0.3f), std::nullopt));
        }
    }

    command_buffer.Draw(foreground_pass);
    command_buffer.EndRendering();
    batch.Push(std::move(command_buffer));
    return true;
}

auto Canvas::Meta() -> std::optional<FrameMeta> {
    return FrameMeta{.extent = {width_, height_}, .format = format_};
}

auto Canvas::OnHover(AppState&, const PointerHit& hit) -> std::optional<Haptics> {
    highlight_dirty_ = true;
    const auto previous = hovered_[hit.pointer];
    hovered_[hit.pointer] = InteractiveAt(hit.uv);
    if (previous == hovered_[hit.pointer]) {
        return std::nullopt;
    }
    return Haptics{.intensity = 0.1f, .duration = 0.01f, .frequency = 5.0f};
}

auto Canvas::OnLeft(AppState&, size_t pointer) -> void {
    highlight_dirty_ = true;
    hovered_[pointer].reset();
}

auto Canvas::OnPointer(AppState& app, const PointerHit& hit, bool pressed) -> void {
    const auto index = pressed ? InteractiveAt(hit.uv) : pressed_[hit.pointer];
    if (!index) {
        return;
    }
    auto& control = *controls_[*index];
    highlight_dirty_ = true;
    if (pressed) {
        pressed_[hit.pointer] = index;
        control.OnPress(app, hit.mode);
    } else {
        pressed_[hit.pointer].reset();
        released_.push_back({.index = *index, .released = std::chrono::steady_clock::now()});
        control.OnRelease(app);
    }
}

auto Canvas::OnScroll(AppState& app, const PointerHit& hit, float delta_y, float delta_x) -> void {
    if (const auto index = hovered_[hit.pointer]) {
        controls_[*index]->OnScroll(app, delta_y, delta_x);
    }
}
