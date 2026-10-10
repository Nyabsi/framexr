// FrameXR
//
// This is a port of wlx-capture from Rust to C++.
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
// Copyright (C) 2023-2025 galister and wlx-capture contributors
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
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

constexpr uint32_t kDrmFormatArgb8888 = 0x34325241;
constexpr uint32_t kDrmFormatAbgr8888 = 0x34324241;
constexpr uint32_t kDrmFormatXrgb8888 = 0x34325258;
constexpr uint32_t kDrmFormatXbgr8888 = 0x34324258;
constexpr uint32_t kDrmFormatAbgr2101010 = 0x30334241;
constexpr uint32_t kDrmFormatXbgr2101010 = 0x30334258;
constexpr uint64_t kDrmFormatModInvalid = 0x00ff'ffff'ffff'ffff;

enum class FrameTransform : uint8_t {
    Undefined,
    Normal,
    Rotated90,
    Rotated180,
    Rotated270,
    Flipped,
    Flipped90,
    Flipped180,
    Flipped270,
};

struct FrameFormat {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fourcc = 0;
    uint64_t modifier = 0;
    FrameTransform transform = FrameTransform::Undefined;
};

struct FramePlane {
    int fd = -1;
    uint32_t offset = 0;
    int32_t stride = 0;
};

struct MouseMeta {
    float x = 0.0f;
    float y = 0.0f;
};

struct DmabufFrame {
    FrameFormat format;
    std::vector<FramePlane> planes;
    std::optional<MouseMeta> mouse;

    [[nodiscard]] auto IsValid() const -> bool;
};

struct MemFdFrame {
    FrameFormat format;
    FramePlane plane;
    std::optional<MouseMeta> mouse;
};

struct MemPtrFrame {
    FrameFormat format;
    const std::byte* data = nullptr;
    size_t size = 0;
    std::optional<MouseMeta> mouse;
};

using Frame = std::variant<DmabufFrame, MemFdFrame, MemPtrFrame>;

struct DrmFormat {
    uint32_t fourcc = 0;
    std::vector<uint64_t> modifiers;
};

[[nodiscard]] auto FourccToString(uint32_t fourcc) -> std::string;
