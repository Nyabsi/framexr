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

#include "Desktop.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>
#include <limits>
#include <numbers>
#include <ranges>
#include <utility>

#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "CommandBatch.hpp"
#include "CommandBuffer.hpp"
#include "Config.hpp"
#include "ConfigStore.hpp"
#include "DbusNotificationSender.hpp"
#include "Device.hpp"
#include "HidProvider.hpp"
#include "Image.hpp"
#include "Interaction.hpp"
#include "FrameTransforms.hpp"
#include "Logger.hpp"
#include "Compose.hpp"
#include "Pipeline.hpp"
#include "ScreenInteraction.hpp"
#include "ScreenRenderer.hpp"
#include "Shaders.hpp"
#include "Yaml.hpp"
#include "framexr_config.h"

#if FRAMEXR_FEATURE_WAYLAND
#include "Notifications.hpp"
#include "PipewireCapture.hpp"
#include "WaylandClient.hpp"
#include "WlrDmabufCapture.hpp"
#include "WlrScreencopyCapture.hpp"
#endif

#if FRAMEXR_FEATURE_X11
#include "XshmCapture.hpp"
#endif

#ifdef _WIN32
#include "DxgiCapture.hpp"
#endif

namespace
{

constexpr float kScreenDistance = 1.0f;

constexpr float kScreenGap = 0.05f;

constexpr float kScreenTilt = 0.1745f;

#if FRAMEXR_FEATURE_X11 && FRAMEXR_FEATURE_PIPEWIRE

auto BestMatch(const PipewireStream& stream, const std::vector<std::shared_ptr<XshmScreen>>& screens) -> std::shared_ptr<XshmScreen>
{
    if (screens.empty())
    {
        return nullptr;
    }
    auto best = screens.front();
    LOG_DEBUG("stream: {}", stream.position ? std::format("({}, {})", stream.position->x(), stream.position->y()) : "None");
    if (!stream.position)
    {
        return best;
    }
    const auto distance = [&](const XshmScreen& screen) -> int32_t
    { return std::abs(screen.x - stream.position->x()) + std::abs(screen.y - stream.position->y()); };
    int32_t best_distance = distance(*best);
    for (const auto& screen : screens | std::views::drop(1))
    {
        LOG_DEBUG("checking: {} {}x{}+{}+{}", screen->name, screen->width, screen->height, screen->x, screen->y);
        if (const int32_t current = distance(*screen); current < best_distance)
        {
            best = screen;
            best_distance = current;
        }
    }
    LOG_DEBUG("best: {}", best->name);
    return best;
}

#endif

} // namespace

Desktop::Desktop(
    std::string name,
    const Eigen::Vector2i& resolution,
    FrameTransform transform,
    const Eigen::Vector2i& position,
    std::unique_ptr<ScreenRenderer> screen,
    ScreenInteraction input
)
    : View(std::make_unique<Compose>(std::move(screen))),
      m_input(input),
      m_position(position)
{
    constexpr float kPi = std::numbers::pi_v<float>;
    float angle = 0.0f;
    if (g_config->upright_screen_fix)
    {
        if (FrameTransforms::IsRotated90(transform))
        {
            angle = kPi / 2.0f;
        }
        else if (FrameTransforms::IsRotated180(transform))
        {
            angle = kPi;
        }
        else if (FrameTransforms::IsRotated270(transform))
        {
            angle = -kPi / 2.0f;
        }
    }

    const auto width = static_cast<float>(resolution.x());
    const auto height = static_cast<float>(resolution.y());
    const Eigen::Vector2f center(0.5f, 0.5f);
    const Eigen::Vector2f unit_x = Eigen::Vector2f::UnitX();
    const Eigen::Vector2f unit_y = Eigen::Vector2f::UnitY();

    Eigen::Affine2f interaction_transform;
    if (FrameTransforms::IsRotated90(transform))
    {
        interaction_transform = FrameTransforms::Affine(-unit_y * (width / height), -unit_x, center);
    }
    else if (FrameTransforms::IsRotated180(transform))
    {
        interaction_transform = FrameTransforms::Affine(-unit_x, -unit_y * (-width / height), center);
    }
    else if (FrameTransforms::IsRotated270(transform))
    {
        interaction_transform = FrameTransforms::Affine(unit_y * (width / height), unit_x, center);
    }
    else if (resolution.y() > resolution.x())
    {
        interaction_transform = FrameTransforms::Affine(unit_x * (height / width), -unit_y, center);
    }
    else
    {
        interaction_transform = FrameTransforms::Affine(unit_x, -unit_y * (width / height), center);
    }

    SetName(std::move(name));
    SetGrabbable(true);
    SetRecenter(true);
    SetInteractable(true);
    SetSpawnScale(1.5f * g_config->desktop_view_scale);
    SetSpawnPoint(Eigen::Vector3f(0.0f, 0.0f, -kScreenDistance));
    SetSpawnRotation(Eigen::Quaternionf(Eigen::AngleAxisf(angle, Eigen::Vector3f::UnitZ())));
    SetExtentHint(VkExtent2D{.width = static_cast<uint32_t>(resolution.x()), .height = static_cast<uint32_t>(resolution.y())});
    SetInteractionTransform(interaction_transform);
}

auto Desktop::ArrangeInFrontOfHead(std::span<const std::unique_ptr<Desktop>> desktops) -> void
{
    std::vector<Desktop*> ordered;
    for (const auto& desktop : desktops)
    {
        ordered.push_back(desktop.get());
    }
    std::ranges::sort(
        ordered,
        [](const Desktop* a, const Desktop* b) -> bool
        {
            const auto& left = a->m_position;
            const auto& right = b->m_position;
            return left.x() != right.x() ? left.x() < right.x() : left.y() < right.y();
        }
    );

    const float middle = (static_cast<float>(ordered.size()) - 1.0f) / 2.0f;
    for (size_t i = 0; i < ordered.size(); ++i)
    {
        auto& state = *ordered[i];
        const float step = 2.0f * std::atan((state.SpawnScale() + kScreenGap) / (2.0f * kScreenDistance));
        const float angle = (static_cast<float>(i) - middle) * step;
        state.SetSpawnPoint(
            kScreenDistance
            * Eigen::Vector3f(
                std::sin(angle) * std::cos(kScreenTilt),
                -std::sin(kScreenTilt),
                -std::cos(angle) * std::cos(kScreenTilt)
            )
        );
        state.SetSpawnRotation(
            Eigen::Quaternionf(Eigen::AngleAxisf(-angle, Eigen::Vector3f::UnitY()))
            * Eigen::Quaternionf(Eigen::AngleAxisf(-kScreenTilt, Eigen::Vector3f::UnitX())) * state.SpawnRotation()
        );
    }
}

#if FRAMEXR_FEATURE_WAYLAND

auto Desktop::SelectScreen(std::string_view instructions, const ScreenCastOptions& options) -> Result<ScreenSelection>
{
    auto future = ScreenCastPortal::SelectScreen(options);
    if (future.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready)
    {
        return future.get();
    }

    LOG_INFO("{}", instructions);
    std::unique_ptr<DbusNotificationSender> sender;
    std::optional<uint32_t> notification;
    if (auto created = DbusNotificationSender::Create())
    {
        sender = std::move(*created);
        if (auto id = sender->NotifySend(instructions, "", 2, 0, 0, true))
        {
            notification = *id;
        }
    }

    auto result = future.get();
    if (sender && notification)
    {
        if (const auto closed = sender->NotifyClose(*notification); !closed)
        {
            LOG_DEBUG("Failed to close notification: {}", closed.error().Message());
        }
    }
    return result;
}

auto Desktop::CreateCompose(
    const WaylandOutput& output,
    bool has_wlr_dmabuf,
    bool has_wlr_screencopy,
    std::map<std::string, std::string>& tokens
) -> std::unique_ptr<ScreenRenderer>
{
    const auto& capture_method = g_config->capture_method;
    std::unique_ptr<Capture> capture;
    if (capture_method == "wlr-dmabuf" && has_wlr_dmabuf)
    {
        LOG_INFO("{}: Using Wlr DMA-Buf", output.name);
        if (auto connection = WaylandClient::Connect())
        {
            capture = std::make_unique<WlrDmabufCapture>(std::move(connection), output.id);
        }
    }
    if (capture_method == "screencopy" && has_wlr_screencopy)
    {
        LOG_INFO("{}: Using Wlr Screencopy Wl-SHM", output.name);
        if (auto connection = WaylandClient::Connect())
        {
            capture = std::make_unique<WlrScreencopyCapture>(std::move(connection), output.id);
        }
    }

    if (!capture)
    {
        LOG_INFO("{}: Using Pipewire capture", output.name);
        std::optional<std::string> token;
        if (const auto it = tokens.find(output.name); it != tokens.end())
        {
            token = it->second;
            LOG_INFO("Found existing Pipewire token for display {}: {}", output.name, it->second);
        }

        auto selection = SelectScreen(
            std::format(
                "Now select: {} {} {} @ {},{}",
                output.name,
                output.make,
                output.model,
                output.logical_position.x(),
                output.logical_position.y()
            ),
            {
                .restore_token = std::move(token),
                .embed_mouse = !g_config->double_cursor_fix,
                .screens_only = true,
                .persist = true,
                .multiple = false,
            }
        );
        if (!selection)
        {
            LOG_WARN("{}: Failed to create Pipewire capture: {}", output.name, selection.error().Message());
        }
        else if (selection->streams.empty())
        {
            LOG_WARN("{}: Failed to create Pipewire capture: no streams selected", output.name);
        }
        else
        {
            capture = std::make_unique<PipewireCapture>(output.name, selection->streams.front().node_id);
            if (selection->restore_token)
            {
                auto& stored = tokens[output.name];
                if (stored != *selection->restore_token)
                {
                    stored = *selection->restore_token;
                    LOG_INFO("Adding Pipewire token {}", stored);
                }
            }
        }
    }

    if (!capture)
    {
        return nullptr;
    }
    return std::make_unique<ScreenRenderer>(output.name, std::move(capture));
}

auto Desktop::CreateWayland(WaylandClient& client, std::span<Desktop* const> existing) -> std::vector<std::unique_ptr<Desktop>>
{
    std::vector<std::unique_ptr<Desktop>> screens;
    auto tokens = ConfigStore::PipewireTokens().value_or(std::map<std::string, std::string>{});
    const auto original_tokens = tokens;
    const bool has_wlr_dmabuf = client.DmabufManager() != nullptr;
    const bool has_wlr_screencopy = client.ScreencopyManager() != nullptr;

    for (const auto& output : client.Outputs())
    {
        if (std::ranges::any_of(existing, [&](const Desktop* desktop) -> bool { return desktop->Name() == output.name; }))
        {
            continue;
        }

        LOG_INFO(
            "{}: Init screen of res ({}, {}), logical ({}, {}) at ({}, {})",
            output.name,
            output.size.x(),
            output.size.y(),
            output.logical_size.x(),
            output.logical_size.y(),
            output.logical_position.x(),
            output.logical_position.y()
        );

        auto renderer = CreateCompose(output, has_wlr_dmabuf, has_wlr_screencopy, tokens);
        if (!renderer)
        {
            continue;
        }

        const auto transform = WaylandClient::ToFrameTransform(output.transform);
        auto desktop = std::make_unique<Desktop>(
            output.name,
            output.logical_size,
            transform,
            output.logical_position,
            std::move(renderer),
            ScreenInteraction(output.logical_position.cast<float>(), output.logical_size.cast<float>(), transform)
        );
        desktop->SetOutput(output.id);
        screens.push_back(std::move(desktop));
    }

    if (tokens != original_tokens)
    {
        if (const auto result = ConfigStore::SavePipewireTokens(tokens); !result)
        {
            LOG_ERROR("Failed to save Pipewire token config: {}", result.error().Message());
        }
    }

    g_hid->SetDesktopExtent(client.DesktopExtent().cast<float>());
    g_hid->SetDesktopOrigin(client.DesktopOrigin().cast<float>());
    return screens;
}

#else

auto Desktop::SelectScreen(std::string_view, const ScreenCastOptions&) -> Result<ScreenSelection>
{
    return Unexpected("Pipewire support not enabled");
}

auto Desktop::CreateCompose(const WaylandOutput&, bool, bool, std::map<std::string, std::string>&)
    -> std::unique_ptr<ScreenRenderer>
{
    return nullptr;
}

auto Desktop::CreateWayland(WaylandClient&, std::span<Desktop* const>) -> std::vector<std::unique_ptr<Desktop>>
{
    return {};
}

#endif

#if FRAMEXR_FEATURE_X11 && FRAMEXR_FEATURE_PIPEWIRE

auto Desktop::CreateX11Pipewire() -> Result<std::vector<std::unique_ptr<Desktop>>>
{
    auto tokens = ConfigStore::PipewireTokens().value_or(std::map<std::string, std::string>{});
    const auto original_tokens = tokens;
    std::optional<std::string> token;
    if (const auto it = tokens.find("x11"); it != tokens.end())
    {
        token = it->second;
    }

    ASSIGN_OR_RETURN(
        auto selection,
        SelectScreen(
            "Select ALL screens on the screencast pop-up!",
            {
                .restore_token = std::move(token),
                .embed_mouse = !g_config->double_cursor_fix,
                .screens_only = true,
                .persist = true,
                .multiple = true,
            }
        )
    );

    if (selection.restore_token)
    {
        auto& stored = tokens["x11"];
        if (stored != *selection.restore_token)
        {
            stored = *selection.restore_token;
            LOG_INFO("Adding Pipewire token {}", stored);
        }
    }
    if (tokens != original_tokens)
    {
        if (const auto result = ConfigStore::SavePipewireTokens(tokens); !result)
        {
            LOG_ERROR("Failed to save Pipewire token config: {}", result.error().Message());
        }
    }

    ASSIGN_OR_RETURN(auto monitors, Xshm::Monitors());
    LOG_INFO("Got {} monitors", monitors.size());
    LOG_INFO("Got {} streams", selection.streams.size());

    Eigen::Vector2f extent = Eigen::Vector2f::Zero();
    std::vector<std::unique_ptr<Desktop>> screens;
    for (size_t i = 0; i < selection.streams.size(); ++i)
    {
        const auto& stream = selection.streams[i];
        const auto monitor = BestMatch(stream, monitors);
        if (!monitor)
        {
            FatalError("No X11 monitor available for screencast stream");
        }
        LOG_INFO("Stream {} is {}", i, monitor->name);

        const Eigen::Vector2f position(static_cast<float>(monitor->x), static_cast<float>(monitor->y));
        const Eigen::Vector2f size(static_cast<float>(monitor->width), static_cast<float>(monitor->height));
        extent = extent.cwiseMax(position + size);

        screens.push_back(
            std::make_unique<Desktop>(
                monitor->name,
                Eigen::Vector2i(static_cast<int32_t>(monitor->width), static_cast<int32_t>(monitor->height)),
                FrameTransform::Normal,
                Eigen::Vector2i(monitor->x, monitor->y),
                std::make_unique<ScreenRenderer>(monitor->name, std::make_unique<PipewireCapture>(monitor->name, stream.node_id)),
                ScreenInteraction(position, size, FrameTransform::Normal)
            )
        );
    }

    g_hid->SetDesktopExtent(extent);
    g_hid->SetDesktopOrigin(Eigen::Vector2f::Zero());
    return screens;
}

#else

auto Desktop::CreateX11Pipewire() -> Result<std::vector<std::unique_ptr<Desktop>>>
{
    return Unexpected("Pipewire support not enabled");
}

#endif

#ifdef _WIN32

auto Desktop::CreateWindows() -> Result<std::vector<std::unique_ptr<Desktop>>>
{
    ASSIGN_OR_RETURN(auto outputs, DxgiCapture::Outputs());

    Eigen::Vector2f origin = Eigen::Vector2f::Constant(std::numeric_limits<float>::max());
    Eigen::Vector2f end = Eigen::Vector2f::Constant(std::numeric_limits<float>::lowest());
    std::vector<std::unique_ptr<Desktop>> screens;
    for (auto& output : outputs)
    {
        const Eigen::Vector2f position(static_cast<float>(output.x), static_cast<float>(output.y));
        const Eigen::Vector2f size(static_cast<float>(output.width), static_cast<float>(output.height));
        origin = origin.cwiseMin(position);
        end = end.cwiseMax(position + size);

        LOG_INFO("{}: Init Windows screen of res ({}, {}) at ({}, {})", output.name, output.width, output.height, output.x, output.y);

        screens.push_back(
            std::make_unique<Desktop>(
                output.name,
                Eigen::Vector2i(output.width, output.height),
                output.transform,
                Eigen::Vector2i(output.x, output.y),
                std::make_unique<ScreenRenderer>(output.name, std::make_unique<DxgiCapture>(output.name, std::move(output.device))),
                ScreenInteraction(position, size, output.transform)
            )
        );
    }

    g_hid->SetDesktopExtent(end - origin);
    g_hid->SetDesktopOrigin(origin);
    return screens;
}

#endif

#if FRAMEXR_FEATURE_X11

auto Desktop::CreateXshm() -> Result<std::vector<std::unique_ptr<Desktop>>>
{
    ASSIGN_OR_RETURN(auto monitors, Xshm::Monitors());

    Eigen::Vector2f extent = Eigen::Vector2f::Zero();
    std::vector<std::unique_ptr<Desktop>> screens;
    for (auto& monitor : monitors)
    {
        const Eigen::Vector2f position(static_cast<float>(monitor->x), static_cast<float>(monitor->y));
        const Eigen::Vector2f size(static_cast<float>(monitor->width), static_cast<float>(monitor->height));
        extent = extent.cwiseMax(position + size);

        LOG_INFO(
            "{}: Init X11 screen of res ({}, {}) at ({}, {})",
            monitor->name,
            monitor->width,
            monitor->height,
            monitor->x,
            monitor->y
        );

        const auto name = monitor->name;
        const Eigen::Vector2i desktop_position(monitor->x, monitor->y);
        const Eigen::Vector2i resolution(static_cast<int32_t>(monitor->width), static_cast<int32_t>(monitor->height));
        screens.push_back(
            std::make_unique<Desktop>(
                name,
                resolution,
                FrameTransform::Normal,
                desktop_position,
                std::make_unique<ScreenRenderer>(name, std::make_unique<XshmCapture>(std::move(monitor))),
                ScreenInteraction(position, size, FrameTransform::Normal)
            )
        );
    }

    g_hid->SetDesktopExtent(extent);
    g_hid->SetDesktopOrigin(Eigen::Vector2f::Zero());
    return screens;
}

#else

auto Desktop::CreateXshm() -> Result<std::vector<std::unique_ptr<Desktop>>>
{
    return Unexpected("X11 support not enabled");
}

#endif

auto Desktop::OnHover(const PointerHit& hit) -> std::optional<Haptics>
{
    m_input.Hover(hit);
    return std::nullopt;
}

auto Desktop::OnPointer(const PointerHit& hit, bool pressed) -> void
{
    m_input.Press(hit, pressed);
}

auto Desktop::OnScroll(const PointerHit&, float delta_y, float delta_x) -> void
{
    ScreenInteraction::Scroll(delta_y, delta_x);
}
