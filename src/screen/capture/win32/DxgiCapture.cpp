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

#include "DxgiCapture.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <optional>
#include <span>
#include <utility>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include "Logger.hpp"

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT kAcquireTimeoutMs = 100;
constexpr auto kRetryDelay = std::chrono::seconds(1);
constexpr auto kPausedDelay = std::chrono::milliseconds(50);

auto Narrow(std::wstring_view text) -> std::string {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string narrow(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), narrow.data(), size, nullptr, nullptr);
    return narrow;
}

auto Transform(DXGI_MODE_ROTATION rotation) -> FrameTransform {
    switch (rotation) {
    case DXGI_MODE_ROTATION_ROTATE90: return FrameTransform::Rotated90;
    case DXGI_MODE_ROTATION_ROTATE180: return FrameTransform::Rotated180;
    case DXGI_MODE_ROTATION_ROTATE270: return FrameTransform::Rotated270;
    default: return FrameTransform::Normal;
    }
}

auto FindOutput(const std::wstring& device, ComPtr<IDXGIAdapter1>& adapter_out) -> ComPtr<IDXGIOutput1> {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return nullptr;
    }
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
            DXGI_OUTPUT_DESC desc{};
            output->GetDesc(&desc);
            ComPtr<IDXGIOutput1> output1;
            if (device == desc.DeviceName && SUCCEEDED(output.As(&output1))) {
                adapter_out = adapter;
                return output1;
            }
        }
    }
    return nullptr;
}

auto Duplicate(const std::string& name, const std::wstring& device) -> std::optional<Duplication> {
    ComPtr<IDXGIAdapter1> adapter;
    auto output = FindOutput(device, adapter);
    if (!output) {
        LOG_ERROR("{}: output is gone", name);
        return std::nullopt;
    }
    Duplication made;
    constexpr D3D_FEATURE_LEVEL kLevel = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(
            adapter.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            0,
            &kLevel,
            1,
            D3D11_SDK_VERSION,
            &made.device,
            nullptr,
            &made.context
        ))) {
        LOG_ERROR("{}: could not create a D3D11 device", name);
        return std::nullopt;
    }
    if (const HRESULT result = output->DuplicateOutput(made.device.Get(), &made.duplication); FAILED(result)) {
        LOG_ERROR("{}: DuplicateOutput failed (0x{:08x})", name, static_cast<uint32_t>(result));
        return std::nullopt;
    }
    DXGI_OUTDUPL_DESC desc{};
    made.duplication->GetDesc(&desc);
    DXGI_OUTPUT_DESC output_desc{};
    output->GetDesc(&output_desc);
    made.rotation = Transform(desc.Rotation);
    made.desktop_width = output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left;
    made.desktop_height = output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top;
    LOG_INFO(
        "{}: duplicating {}x{} (desktop {}x{}, rotation {})",
        name,
        desc.ModeDesc.Width,
        desc.ModeDesc.Height,
        made.desktop_width,
        made.desktop_height,
        static_cast<int>(desc.Rotation)
    );
    return made;
}

auto PrepareStaging(Duplication& duplication, const D3D11_TEXTURE2D_DESC& source) -> bool {
    if (duplication.staging && duplication.format.width == source.Width && duplication.format.height == source.Height) {
        return true;
    }
    if (source.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
        LOG_ERROR("Unsupported desktop texture format {}", static_cast<int>(source.Format));
        return false;
    }
    const D3D11_TEXTURE2D_DESC staging{
        .Width = source.Width,
        .Height = source.Height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = source.Format,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .Usage = D3D11_USAGE_STAGING,
        .BindFlags = 0,
        .CPUAccessFlags = D3D11_CPU_ACCESS_READ,
        .MiscFlags = 0,
    };
    duplication.staging.Reset();
    if (FAILED(duplication.device->CreateTexture2D(&staging, nullptr, &duplication.staging))) {
        LOG_ERROR("Could not create a {}x{} staging texture", source.Width, source.Height);
        return false;
    }
    const bool upright = std::cmp_equal(source.Width, duplication.desktop_width)
                      && std::cmp_equal(source.Height, duplication.desktop_height) && source.Width != source.Height;
    duplication.format = FrameFormat{
        .width = source.Width,
        .height = source.Height,
        .fourcc = kDrmFormatXrgb8888,
        .modifier = 0,
        .transform = upright ? FrameTransform::Normal : duplication.rotation,
    };
    LOG_INFO("Desktop texture is {}x{}, transform {}", source.Width, source.Height, static_cast<int>(duplication.format.transform));
    return true;
}

auto PointerMeta(const Duplication& duplication, POINT position) -> std::optional<MouseMeta> {
    if (duplication.desktop_width <= 0 || duplication.desktop_height <= 0) {
        return std::nullopt;
    }
    const float u = static_cast<float>(position.x) / static_cast<float>(duplication.desktop_width);
    const float v = static_cast<float>(position.y) / static_cast<float>(duplication.desktop_height);
    switch (duplication.format.transform) {
    case FrameTransform::Rotated90: return MouseMeta{.x = v, .y = 1.0f - u};
    case FrameTransform::Rotated180: return MouseMeta{.x = 1.0f - u, .y = 1.0f - v};
    case FrameTransform::Rotated270: return MouseMeta{.x = 1.0f - v, .y = u};
    default: return MouseMeta{.x = u, .y = v};
    }
}

auto CopyFrame(Duplication& duplication, IDXGIResource* resource, std::vector<std::byte>& pixels) -> bool {
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&texture)))) {
        return false;
    }
    D3D11_TEXTURE2D_DESC source{};
    texture->GetDesc(&source);
    if (!PrepareStaging(duplication, source)) {
        return false;
    }
    duplication.context->CopyResource(duplication.staging.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(duplication.context->Map(duplication.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return false;
    }
    const size_t row = static_cast<size_t>(duplication.format.width) * 4;
    pixels.resize(row * duplication.format.height);
    const auto* source_bytes = static_cast<const std::byte*>(mapped.pData);
    for (uint32_t y = 0; y < duplication.format.height; ++y) {
        std::memcpy(pixels.data() + y * row, source_bytes + static_cast<size_t>(y) * mapped.RowPitch, row);
    }
    duplication.context->Unmap(duplication.staging.Get(), 0);
    return true;
}

} // namespace

auto DxgiCapture::Outputs() -> Result<std::vector<DxgiOutput>> {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return Unexpected("Could not create a DXGI factory");
    }
    std::vector<DxgiOutput> outputs;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
            DXGI_OUTPUT_DESC desc{};
            output->GetDesc(&desc);
            if (desc.AttachedToDesktop == FALSE) {
                continue;
            }
            std::wstring device(desc.DeviceName);
            auto name = Narrow(device);
            if (name.starts_with("\\\\.\\")) {
                name.erase(0, 4);
            }
            const auto& rect = desc.DesktopCoordinates;
            outputs.push_back({
                .name = std::move(name),
                .device = std::move(device),
                .x = rect.left,
                .y = rect.top,
                .width = rect.right - rect.left,
                .height = rect.bottom - rect.top,
                .transform = Transform(desc.Rotation),
            });
        }
    }
    if (outputs.empty()) {
        return Unexpected("No monitors are attached to the desktop");
    }
    return outputs;
}

DxgiCapture::DxgiCapture(std::string name, std::wstring device) : m_name(std::move(name)), m_device(std::move(device)) {
}

DxgiCapture::~DxgiCapture() {
    m_running.store(false);
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

auto DxgiCapture::Start(std::span<const DrmFormat>, FrameProcessor processor) -> void {
    m_channel = std::make_shared<FrameChannel<std::unique_ptr<ProcessedFrame>>>(2);
    m_processor = std::move(processor);
    m_thread = std::thread(&DxgiCapture::Run, this);
}

auto DxgiCapture::Receive() -> std::unique_ptr<ProcessedFrame> {
    if (!m_channel) {
        return nullptr;
    }
    auto frames = m_channel->TakeAll();
    if (frames.empty()) {
        return nullptr;
    }
    return std::move(frames.back());
}

auto DxgiCapture::Pause() -> void {
    m_paused.store(true);
}

auto DxgiCapture::Resume() -> void {
    if (m_channel) {
        m_channel->TakeAll();
    }
    m_refresh.store(true);
    m_paused.store(false);
}

auto DxgiCapture::Run() -> void {
    std::optional<Duplication> duplication;
    std::vector<std::byte> pixels;
    std::optional<MouseMeta> mouse;

    while (m_running.load()) {
        if (m_paused.load()) {
            std::this_thread::sleep_for(kPausedDelay);
            continue;
        }
        if (!duplication) {
            duplication = Duplicate(m_name, m_device);
            if (!duplication) {
                std::this_thread::sleep_for(kRetryDelay);
                continue;
            }
            pixels.clear();
            m_refresh.store(true);
        }

        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> resource;
        const HRESULT acquired = duplication->duplication->AcquireNextFrame(kAcquireTimeoutMs, &info, &resource);
        if (acquired == DXGI_ERROR_WAIT_TIMEOUT) {
            continue;
        }
        if (FAILED(acquired)) {
            LOG_INFO("{}: duplication lost (0x{:08x}), recreating", m_name, static_cast<uint32_t>(acquired));
            duplication.reset();
            continue;
        }

        bool changed = m_refresh.exchange(false);
        if ((pixels.empty() || info.LastPresentTime.QuadPart != 0) && CopyFrame(*duplication, resource.Get(), pixels)) {
            changed = true;
        }
        if (info.LastMouseUpdateTime.QuadPart != 0) {
            mouse = info.PointerPosition.Visible != FALSE ? PointerMeta(*duplication, info.PointerPosition.Position) : std::nullopt;
            changed = true;
        }
        resource.Reset();
        duplication->duplication->ReleaseFrame();

        if (!changed || pixels.empty()) {
            continue;
        }
        MemPtrFrame frame{
            .format = duplication->format,
            .data = pixels.data(),
            .size = pixels.size(),
            .mouse = mouse,
        };
        if (auto processed = m_processor(Frame(frame))) {
            m_channel->Push(std::move(processed));
        }
    }
}
