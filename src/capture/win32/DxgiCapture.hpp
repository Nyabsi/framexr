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

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include "Capture.hpp"
#include "Error.hpp"
#include "Frame.hpp"
#include "FrameChannel.hpp"

struct Duplication {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    FrameFormat format;
    FrameTransform rotation = FrameTransform::Normal;
    LONG desktop_width = 0;
    LONG desktop_height = 0;
};

struct DxgiOutput {
    std::string name;
    std::wstring device;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    FrameTransform transform = FrameTransform::Normal;
};

class DxgiCapture final : public Capture {
  public:
    static auto Outputs() -> Result<std::vector<DxgiOutput>>;

    DxgiCapture(std::string name, std::wstring device);
    ~DxgiCapture() override;
    DxgiCapture(const DxgiCapture&) = delete;
    auto operator=(const DxgiCapture&) -> DxgiCapture& = delete;

    auto Start(std::span<const DrmFormat> dmabuf_formats, FrameProcessor processor) -> void override;
    [[nodiscard]] auto IsStarted() const -> bool override { return m_channel != nullptr; }
    [[nodiscard]] auto SupportsDmabuf() const -> bool override { return false; }
    auto Receive() -> std::unique_ptr<ProcessedFrame> override;
    auto Pause() -> void override;
    auto Resume() -> void override;
    auto RequestNewFrame() -> void override {}

  private:
    auto Run() -> void;

    std::string m_name;
    std::wstring m_device;
    FrameProcessor m_processor;
    std::shared_ptr<FrameChannel<std::unique_ptr<ProcessedFrame>>> m_channel;
    std::atomic<bool> m_running{true};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_refresh{true};
    std::thread m_thread;
};
