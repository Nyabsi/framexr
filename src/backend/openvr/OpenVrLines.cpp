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

#include "OpenVrLines.hpp"

#include <array>
#include <cstddef>
#include <format>
#include <numbers>

#include "Device.hpp"
#include "Logger.hpp"
#include "OpenVrStaticRenderer.hpp"
#include "SplitOverlayBackend.hpp"

auto OpenVrLines::Create() -> Result<OpenVrLines> {
    ASSIGN_OR_RETURN(auto command_buffer, g_device->CreateCommandBuffer(g_device->GraphicsQueue()));
    const std::array<std::byte, 16> white{
        std::byte{255},
        std::byte{255},
        std::byte{255},
        std::byte{255},
        std::byte{255},
        std::byte{255},
        std::byte{255},
        std::byte{255},
        std::byte{255},
        std::byte{255},
        std::byte{255},
        std::byte{255},
        std::byte{255},
        std::byte{255},
        std::byte{255},
        std::byte{255},
    };
    ASSIGN_OR_RETURN(auto image, command_buffer.UploadImage(2, 2, VK_FORMAT_R8G8B8A8_UNORM, white));
    command_buffer.TransitionLayout(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    RETURN_IF_ERROR(command_buffer.SubmitAndWait());
    return OpenVrLines(std::move(image));
}

OpenVrLines::OpenVrLines(std::shared_ptr<Image> image)
    : m_image(std::move(image)),
      m_colors{
          Eigen::Vector4f(1.0f, 1.0f, 1.0f, 1.0f),
          Eigen::Vector4f(0.0f, 0.375f, 0.5f, 1.0f),
          Eigen::Vector4f(0.69f, 0.188f, 0.0f, 1.0f),
          Eigen::Vector4f(0.375f, 0.0f, 0.5f, 1.0f),
          Eigen::Vector4f(1.0f, 0.0f, 0.0f, 1.0f),
      } {
}

auto OpenVrLines::Allocate() -> size_t {
    const size_t id = m_lines.empty() ? 1 : m_lines.back().id + 1;

    OverlayState state;
    state.SetName(std::format("framexr-line{}", id));
    state.SetShowHide(true);
    state.SetZOrder(kZOrderLines);
    state.SetDirty(true);

    m_lines.push_back({
        .id = id,
        .overlay = std::make_unique<Overlay>(
            std::move(state),
            std::make_unique<SplitOverlayBackend>(std::make_unique<OpenVrStaticRenderer>(m_image), nullptr)
        ),
        .data = {
            .width = 0.002f,
            .override_width = true,
            .image = m_image,
            .image_dirty = true,
        },
    });
    return id;
}

auto OpenVrLines::DrawFrom(size_t id, Eigen::Affine3f from, float length, size_t color, const Eigen::Affine3f& hmd) -> void {
    const auto rotation = Eigen::Affine3f(Eigen::AngleAxisf(-std::numbers::pi_v<float> * 0.5f, Eigen::Vector3f::UnitX()));

    from.translation() += from.linear() * -Eigen::Vector3f::UnitZ() * (length * 0.5f);
    Eigen::Affine3f transform = from * rotation * Eigen::Scaling(1.0f, length / 0.002f, 1.0f);

    const Eigen::Vector3f to_hmd = hmd.translation() - from.translation();
    const std::array<Eigen::Vector3f, 4> sides{
        Eigen::Vector3f::UnitZ(),
        Eigen::Vector3f::UnitX(),
        -Eigen::Vector3f::UnitZ(),
        -Eigen::Vector3f::UnitX(),
    };
    const std::array<float, 4> angles{0.0f, 0.5f, 1.0f, 1.5f};

    size_t closest = 0;
    float closest_dot = 0.0f;
    for (size_t i = 0; i < sides.size(); ++i) {
        const float dot = to_hmd.dot(transform.linear() * sides[i]);
        if (i == 0 || dot > closest_dot) {
            closest = i;
            closest_dot = dot;
        }
    }
    transform = transform * Eigen::AngleAxisf(std::numbers::pi_v<float> * angles[closest], Eigen::Vector3f::UnitY());

    for (auto& line : m_lines) {
        if (line.id == id) {
            line.overlay->State().SetWantVisible(true);
            line.overlay->State().SetTransform(transform);
            line.data.color = m_colors.at(color);
            return;
        }
    }
    LOG_WARN("Line {} does not exist", id);
}

auto OpenVrLines::Hide(size_t id) -> void {
    for (auto& line : m_lines) {
        if (line.id == id) {
            line.overlay->State().SetWantVisible(false);
        }
    }
}

auto OpenVrLines::Update(vr::ETrackingUniverseOrigin universe) -> Result<> {
    for (auto& line : m_lines) {
        RETURN_IF_ERROR(OpenVrOverlay::AfterInput(*line.overlay, line.data));
        if (!line.overlay->State().WantVisible()) {
            continue;
        }
        if (line.overlay->State().Dirty()) {
            OpenVrOverlay::UploadTexture(*line.overlay, line.data);
            line.overlay->State().SetDirty(false);
        }
        OpenVrOverlay::UploadTransform(universe, *line.overlay, line.data);
        OpenVrOverlay::UploadColor(*line.overlay, line.data);
    }
    return {};
}

auto OpenVrLines::MarkDirty() -> void {
    for (auto& line : m_lines) {
        line.overlay->State().SetDirty(true);
    }
}
