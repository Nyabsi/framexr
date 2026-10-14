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

#include "ColorPickerComponent.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <numbers>
#include <utility>
#include <vector>

#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "FormTheme.hpp"
#include "OutlineComponent.hpp"
#include "SpriteComponent.hpp"

namespace
{

constexpr uint32_t kWheelPixels = 192;
constexpr uint32_t kBarPixels = 256;
constexpr uint32_t kBarPixelHeight = 8;

auto Rgb(float hue, float saturation, float value) -> std::array<float, 3>
{
    const float c = value * saturation;
    const float h = std::fmod(std::fmod(hue, 1.0f) + 1.0f, 1.0f) * 6.0f;
    const float x = c * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));
    std::array<float, 3> rgb{};
    if (h < 1.0f)
    {
        rgb = {c, x, 0.0f};
    }
    else if (h < 2.0f)
    {
        rgb = {x, c, 0.0f};
    }
    else if (h < 3.0f)
    {
        rgb = {0.0f, c, x};
    }
    else if (h < 4.0f)
    {
        rgb = {0.0f, x, c};
    }
    else if (h < 5.0f)
    {
        rgb = {x, 0.0f, c};
    }
    else
    {
        rgb = {c, 0.0f, x};
    }
    const float m = value - c;
    return {rgb[0] + m, rgb[1] + m, rgb[2] + m};
}

auto ToByte(float value) -> std::byte
{
    return static_cast<std::byte>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

auto Upload(uint32_t width, uint32_t height, const std::vector<std::byte>& pixels) -> std::shared_ptr<Image>
{
    if (g_device == nullptr)
    {
        return nullptr;
    }
    auto command_buffer = g_device->CreateCommandBuffer(g_device->TransferQueue());
    if (!command_buffer)
    {
        return nullptr;
    }
    auto image = command_buffer->UploadImage(width, height, VK_FORMAT_R8G8B8A8_UNORM, pixels);
    if (!image || !command_buffer->SubmitUpload())
    {
        return nullptr;
    }
    return std::move(*image);
}

} // namespace

auto ColorPickerComponent::Hex(float hue, float saturation, float value) -> std::string
{
    const auto rgb = Rgb(hue, saturation, value);
    const auto byte = [](float channel) -> int { return static_cast<int>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f)); };
    return std::format("#{:02x}{:02x}{:02x}", byte(rgb[0]), byte(rgb[1]), byte(rgb[2]));
}

auto ColorPickerComponent::Hsv(const std::string& hex) -> std::array<float, 3>
{
    const auto color = FormTheme::Rgba(hex);
    const float r = color.x();
    const float g = color.y();
    const float b = color.z();
    const float high = std::max({r, g, b});
    const float delta = high - std::min({r, g, b});
    float hue = 0.0f;
    if (delta > 0.0f)
    {
        if (high == r)
        {
            hue = std::fmod((g - b) / delta, 6.0f);
        }
        else if (high == g)
        {
            hue = (b - r) / delta + 2.0f;
        }
        else
        {
            hue = (r - g) / delta + 4.0f;
        }
        hue /= 6.0f;
        if (hue < 0.0f)
        {
            hue += 1.0f;
        }
    }
    return {hue, high > 0.0f ? delta / high : 0.0f, high};
}

auto ColorPickerComponent::WheelImage(float value) -> std::shared_ptr<Image>
{
    std::vector<std::byte> pixels(size_t{kWheelPixels} * kWheelPixels * 4);
    const float radius = static_cast<float>(kWheelPixels) / 2.0f;
    for (uint32_t y = 0; y < kWheelPixels; ++y)
    {
        for (uint32_t x = 0; x < kWheelPixels; ++x)
        {
            const float dx = static_cast<float>(x) + 0.5f - radius;
            const float dy = static_cast<float>(y) + 0.5f - radius;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float hue = std::atan2(dy, dx) / (2.0f * std::numbers::pi_v<float>);
            const auto rgb = Rgb(hue, std::min(distance / (radius - 1.0f), 1.0f), value);
            const size_t offset = (size_t{y} * kWheelPixels + x) * 4;
            pixels[offset] = ToByte(rgb[0]);
            pixels[offset + 1] = ToByte(rgb[1]);
            pixels[offset + 2] = ToByte(rgb[2]);
            pixels[offset + 3] = ToByte(radius + 0.5f - distance);
        }
    }
    return Upload(kWheelPixels, kWheelPixels, pixels);
}

auto ColorPickerComponent::BarImage(float hue, float saturation) -> std::shared_ptr<Image>
{
    std::vector<std::byte> pixels(size_t{kBarPixels} * kBarPixelHeight * 4);
    for (uint32_t x = 0; x < kBarPixels; ++x)
    {
        const auto rgb = Rgb(hue, saturation, static_cast<float>(x) / static_cast<float>(kBarPixels - 1));
        for (uint32_t y = 0; y < kBarPixelHeight; ++y)
        {
            const size_t offset = (size_t{y} * kBarPixels + x) * 4;
            pixels[offset] = ToByte(rgb[0]);
            pixels[offset + 1] = ToByte(rgb[1]);
            pixels[offset + 2] = ToByte(rgb[2]);
            pixels[offset + 3] = std::byte{255};
        }
    }
    return Upload(kBarPixels, kBarPixelHeight, pixels);
}

auto ColorPickerComponent::WheelRect(float width) -> CanvasRect
{
    return {(width - kWheelSize) / 2.0f, 0.0f, kWheelSize, kWheelSize};
}

auto ColorPickerComponent::BarRect(float width) -> CanvasRect
{
    return {0.0f, kWheelSize + kGap + 4.0f, width, kBarHeight};
}

ColorPickerComponent::ColorPickerComponent(
    std::string name,
    std::array<float, 3> hsv,
    const std::shared_ptr<Image>& wheel,
    const std::shared_ptr<Image>& bar
)
    : FormComponent(std::move(name)),
      m_hsv(hsv),
      m_wheel(AddComponent<SpriteComponent>("wheel", wheel)),
      m_marker(AddComponent<OutlineComponent>("", std::nullopt, HighlightOutline{.color = Eigen::Vector4f::Ones(), .width = 2.5f})),
      m_bar(AddComponent<SpriteComponent>("brightness", bar)),
      m_knob(AddComponent<OutlineComponent>("", std::nullopt, HighlightOutline{.color = Eigen::Vector4f::Ones(), .width = 2.0f}))
{
    m_marker.SetCornerRadius(7.0f);
    m_knob.SetCornerRadius(3.0f);
    SetSize({0.0f, kWheelSize + kGap + kBarHeight + 8.0f});
}

auto ColorPickerComponent::Layout() -> void
{
    const auto [hue, saturation, value] = m_hsv;
    const auto wheel = WheelRect(Size().x());
    m_wheel.SetRect(wheel);
    const float radius = kWheelSize / 2.0f;
    const float angle = hue * 2.0f * std::numbers::pi_v<float>;
    const Eigen::Vector2f marker = Eigen::Vector2f(wheel.x + radius, wheel.y + radius)
                                 + Eigen::Vector2f(std::cos(angle), std::sin(angle)) * saturation * radius;
    m_marker.SetRect({marker.x() - 7.0f, marker.y() - 7.0f, 14.0f, 14.0f});
    const auto bar = BarRect(Size().x());
    m_bar.SetRect(bar);
    const float knob = bar.x + value * bar.w;
    m_knob.SetRect({knob - 4.0f, bar.y - 4.0f, 8.0f, bar.h + 8.0f});
}
