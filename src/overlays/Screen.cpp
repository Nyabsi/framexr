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

#include "Screen.hpp"

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
#include "DbusNotificationSender.hpp"
#include "Device.hpp"
#include "Hid.hpp"
#include "Image.hpp"
#include "Input.hpp"
#include "Logger.hpp"
#include "OverlayContainer.hpp"
#include "OverlayRenderer.hpp"
#include "Pipeline.hpp"
#include "ScreenInteraction.hpp"
#include "ScreenRenderer.hpp"
#include "Shaders.hpp"
#include "SplitOverlayBackend.hpp"
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

namespace {

constexpr float kScreenDistance = 1.0f;

constexpr float kScreenGap = 0.05f;

constexpr float kScreenTilt = 0.1745f;

auto TokenPath() -> std::filesystem::path {
    return ConfigStore::ConfD() / "pw_tokens.yaml";
}

#if FRAMEXR_FEATURE_X11 && FRAMEXR_FEATURE_PIPEWIRE

auto BestMatch(const PipewireStream& stream, const std::vector<std::shared_ptr<XshmScreen>>& screens)
    -> std::shared_ptr<XshmScreen> {
    if (screens.empty()) {
        return nullptr;
    }
    auto best = screens.front();
    LOG_DEBUG("stream: {}", stream.position ? std::format("({}, {})", stream.position->x(), stream.position->y()) : "None");
    if (!stream.position) {
        return best;
    }
    const auto distance = [&](const XshmScreen& screen) -> int32_t {
        return std::abs(screen.x - stream.position->x()) + std::abs(screen.y - stream.position->y());
    };
    int32_t best_distance = distance(*best);
    for (const auto& screen : screens | std::views::drop(1)) {
        LOG_DEBUG("checking: {} {}x{}+{}+{}", screen->name, screen->width, screen->height, screen->x, screen->y);
        if (const int32_t current = distance(*screen); current < best_distance) {
            best = screen;
            best_distance = current;
        }
    }
    LOG_DEBUG("best: {}", best->name);
    return best;
}

#endif

} // namespace

auto Screens::CreateState(std::string name, const Eigen::Vector2i& resolution, FrameTransform transform) -> OverlayState {
    constexpr float kPi = std::numbers::pi_v<float>;
    float angle = 0.0f;
    if (g_config->upright_screen_fix) {
        if (FrameTransforms::IsRotated90(transform)) {
            angle = kPi / 2.0f;
        } else if (FrameTransforms::IsRotated180(transform)) {
            angle = kPi;
        } else if (FrameTransforms::IsRotated270(transform)) {
            angle = -kPi / 2.0f;
        }
    }

    const auto width = static_cast<float>(resolution.x());
    const auto height = static_cast<float>(resolution.y());
    const Eigen::Vector2f center(0.5f, 0.5f);
    const Eigen::Vector2f unit_x = Eigen::Vector2f::UnitX();
    const Eigen::Vector2f unit_y = Eigen::Vector2f::UnitY();

    Eigen::Affine2f interaction_transform;
    if (FrameTransforms::IsRotated90(transform)) {
        interaction_transform = FrameTransforms::Affine(-unit_y * (width / height), -unit_x, center);
    } else if (FrameTransforms::IsRotated180(transform)) {
        interaction_transform = FrameTransforms::Affine(-unit_x, -unit_y * (-width / height), center);
    } else if (FrameTransforms::IsRotated270(transform)) {
        interaction_transform = FrameTransforms::Affine(unit_y * (width / height), unit_x, center);
    } else if (resolution.y() > resolution.x()) {
        interaction_transform = FrameTransforms::Affine(unit_x * (height / width), -unit_y, center);
    } else {
        interaction_transform = FrameTransforms::Affine(unit_x, -unit_y * (width / height), center);
    }

    OverlayState state;
    state.SetName(std::move(name));
    state.SetGrabbable(true);
    state.SetRecenter(true);
    state.SetInteractable(true);
    state.SetSpawnScale(1.5f * g_config->desktop_view_scale);
    state.SetSpawnPoint(Eigen::Vector3f(0.0f, 0.0f, -kScreenDistance));
    state.SetSpawnRotation(Eigen::Quaternionf(Eigen::AngleAxisf(angle, Eigen::Vector3f::UnitZ())));
    state.SetExtentHint(VkExtent2D{.width = static_cast<uint32_t>(resolution.x()), .height = static_cast<uint32_t>(resolution.y())});
    state.SetInteractionTransform(interaction_transform);
    return state;
}

auto Screens::ArrangeInFrontOfHead(std::vector<ScreenOverlay>& screens) -> void {
    std::vector<ScreenOverlay*> ordered;
    ordered.reserve(screens.size());
    for (auto& screen : screens) {
        ordered.push_back(&screen);
    }
    std::ranges::sort(ordered, [](const ScreenOverlay* a, const ScreenOverlay* b) -> bool {
        const auto& left = a->desktop_position;
        const auto& right = b->desktop_position;
        return left.x() != right.x() ? left.x() < right.x() : left.y() < right.y();
    });

    const float middle = (static_cast<float>(ordered.size()) - 1.0f) / 2.0f;
    for (size_t i = 0; i < ordered.size(); ++i) {
        auto& state = ordered[i]->state;
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

auto Screens::LoadTokens() -> Result<std::map<std::string, std::string>> {
    ASSIGN_OR_RETURN(const auto parsed, ConfigStore::ParseFile(TokenPath()));
    std::map<std::string, std::string> tokens;
    if (const auto it = parsed.find("pw_tokens"); it != parsed.end() && it->is_object()) {
        for (const auto& [display, token] : it->items()) {
            if (token.is_string()) {
                tokens.emplace(display, token.get<std::string>());
            }
        }
    }
    return tokens;
}

auto Screens::SaveTokens(const std::map<std::string, std::string>& tokens) -> Result<> {
    const nlohmann::json document{{"pw_tokens", tokens}};
    std::ofstream stream(TokenPath(), std::ios::binary | std::ios::trunc);
    if (!stream) {
        return Unexpected(std::format("Failed to open {}", TokenPath().string()));
    }
    stream << Yaml::Emit(document);
    if (!stream) {
        return Unexpected(std::format("Failed to write {}", TokenPath().string()));
    }
    return {};
}

#if FRAMEXR_FEATURE_WAYLAND

auto Screens::SelectScreen(std::string_view instructions, const ScreenCastOptions& options) -> Result<ScreenSelection> {
    auto future = ScreenCastPortal::SelectScreen(options);
    if (future.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready) {
        return future.get();
    }

    LOG_INFO("{}", instructions);
    std::unique_ptr<DbusNotificationSender> sender;
    std::optional<uint32_t> notification;
    if (auto created = DbusNotificationSender::Create()) {
        sender = std::move(*created);
        if (auto id = sender->NotifySend(instructions, "", 2, 0, 0, true)) {
            notification = *id;
        }
    }

    auto result = future.get();
    if (sender && notification) {
        if (const auto closed = sender->NotifyClose(*notification); !closed) {
            LOG_DEBUG("Failed to close notification: {}", closed.error().Message());
        }
    }
    return result;
}

auto Screens::CreateWaylandRenderer(
    const WaylandOutput& output,
    bool has_wlr_dmabuf,
    bool has_wlr_screencopy,
    std::map<std::string, std::string>& tokens
) -> std::unique_ptr<ScreenRenderer> {
    const auto& capture_method = g_config->capture_method;
    std::unique_ptr<Capture> capture;
    if (capture_method == "wlr-dmabuf" && has_wlr_dmabuf) {
        LOG_INFO("{}: Using Wlr DMA-Buf", output.name);
        if (auto connection = WaylandClient::Connect()) {
            capture = std::make_unique<WlrDmabufCapture>(std::move(connection), output.id);
        }
    }
    if (capture_method == "screencopy" && has_wlr_screencopy) {
        LOG_INFO("{}: Using Wlr Screencopy Wl-SHM", output.name);
        if (auto connection = WaylandClient::Connect()) {
            capture = std::make_unique<WlrScreencopyCapture>(std::move(connection), output.id);
        }
    }

    if (!capture) {
        LOG_INFO("{}: Using Pipewire capture", output.name);
        std::optional<std::string> token;
        if (const auto it = tokens.find(output.name); it != tokens.end()) {
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
        if (!selection) {
            LOG_WARN("{}: Failed to create Pipewire capture: {}", output.name, selection.error().Message());
        } else if (selection->streams.empty()) {
            LOG_WARN("{}: Failed to create Pipewire capture: no streams selected", output.name);
        } else {
            capture = std::make_unique<PipewireCapture>(output.name, selection->streams.front().node_id);
            if (selection->restore_token) {
                auto& stored = tokens[output.name];
                if (stored != *selection->restore_token) {
                    stored = *selection->restore_token;
                    LOG_INFO("Adding Pipewire token {}", stored);
                }
            }
        }
    }

    if (!capture) {
        return nullptr;
    }
    return std::make_unique<ScreenRenderer>(output.name, std::move(capture));
}

auto Screens::CreateWayland(WaylandClient& client) -> std::vector<ScreenOverlay> {
    std::vector<ScreenOverlay> screens;
    auto tokens = LoadTokens().value_or(std::map<std::string, std::string>{});
    const auto original_tokens = tokens;
    const bool has_wlr_dmabuf = client.DmabufManager() != nullptr;
    const bool has_wlr_screencopy = client.ScreencopyManager() != nullptr;

    for (const auto& output : client.Outputs()) {
        if (std::ranges::any_of(g_overlays->Screens(), [&](const ScreenMeta& screen) -> bool {
                return screen.name == output.name;
            })) {
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

        auto renderer = CreateWaylandRenderer(output, has_wlr_dmabuf, has_wlr_screencopy, tokens);
        if (!renderer) {
            continue;
        }

        const auto transform = WaylandClient::ToFrameTransform(output.transform);
        auto interaction = std::make_unique<ScreenInteraction>(
            output.logical_position.cast<float>(),
            output.logical_size.cast<float>(),
            transform
        );
        auto state = CreateState(output.name, output.logical_size, transform);
        ScreenMeta meta{.name = output.name, .id = state.Id(), .native_handle = output.id};
        auto backend = std::make_unique<SplitOverlayBackend>(std::move(renderer), std::move(interaction));
        screens.push_back({
            .meta = std::move(meta),
            .state = std::move(state),
            .backend = std::move(backend),
            .desktop_position = output.logical_position,
        });
    }

    if (tokens != original_tokens) {
        if (const auto result = SaveTokens(tokens); !result) {
            LOG_ERROR("Failed to save Pipewire token config: {}", result.error().Message());
        }
    }

    g_hid->SetDesktopExtent(client.DesktopExtent().cast<float>());
    g_hid->SetDesktopOrigin(client.DesktopOrigin().cast<float>());
    return screens;
}

#else

auto Screens::SelectScreen(std::string_view, const ScreenCastOptions&) -> Result<ScreenSelection> {
    return Unexpected("Pipewire support not enabled");
}

auto Screens::CreateWaylandRenderer(const WaylandOutput&, bool, bool, std::map<std::string, std::string>&)
    -> std::unique_ptr<ScreenRenderer> {
    return nullptr;
}

auto Screens::CreateWayland(WaylandClient&) -> std::vector<ScreenOverlay> {
    return {};
}

#endif

#if FRAMEXR_FEATURE_X11 && FRAMEXR_FEATURE_PIPEWIRE

auto Screens::CreateX11Pipewire() -> Result<std::vector<ScreenOverlay>> {
    auto tokens = LoadTokens().value_or(std::map<std::string, std::string>{});
    const auto original_tokens = tokens;
    std::optional<std::string> token;
    if (const auto it = tokens.find("x11"); it != tokens.end()) {
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

    if (selection.restore_token) {
        auto& stored = tokens["x11"];
        if (stored != *selection.restore_token) {
            stored = *selection.restore_token;
            LOG_INFO("Adding Pipewire token {}", stored);
        }
    }
    if (tokens != original_tokens) {
        if (const auto result = SaveTokens(tokens); !result) {
            LOG_ERROR("Failed to save Pipewire token config: {}", result.error().Message());
        }
    }

    ASSIGN_OR_RETURN(auto monitors, Xshm::Monitors());
    LOG_INFO("Got {} monitors", monitors.size());
    LOG_INFO("Got {} streams", selection.streams.size());

    Eigen::Vector2f extent = Eigen::Vector2f::Zero();
    std::vector<ScreenOverlay> screens;
    for (size_t i = 0; i < selection.streams.size(); ++i) {
        const auto& stream = selection.streams[i];
        const auto monitor = BestMatch(stream, monitors);
        if (!monitor) {
            FatalError("No X11 monitor available for screencast stream");
        }
        LOG_INFO("Stream {} is {}", i, monitor->name);

        const Eigen::Vector2f position(static_cast<float>(monitor->x), static_cast<float>(monitor->y));
        const Eigen::Vector2f size(static_cast<float>(monitor->width), static_cast<float>(monitor->height));
        extent = extent.cwiseMax(position + size);

        auto state = CreateState(
            monitor->name,
            {static_cast<int32_t>(monitor->width), static_cast<int32_t>(monitor->height)},
            FrameTransform::Normal
        );
        ScreenMeta meta{.name = monitor->name, .id = state.Id(), .native_handle = 0};
        auto backend = std::make_unique<SplitOverlayBackend>(
            std::make_unique<ScreenRenderer>(monitor->name, std::make_unique<PipewireCapture>(monitor->name, stream.node_id)),
            std::make_unique<ScreenInteraction>(position, size, FrameTransform::Normal)
        );
        screens.push_back({
            .meta = std::move(meta),
            .state = std::move(state),
            .backend = std::move(backend),
            .desktop_position = {monitor->x, monitor->y},
        });
    }

    g_hid->SetDesktopExtent(extent);
    g_hid->SetDesktopOrigin(Eigen::Vector2f::Zero());
    return screens;
}

#else

auto Screens::CreateX11Pipewire() -> Result<std::vector<ScreenOverlay>> {
    return Unexpected("Pipewire support not enabled");
}

#endif

#ifdef _WIN32

auto Screens::CreateWindows() -> Result<std::vector<ScreenOverlay>> {
    ASSIGN_OR_RETURN(auto outputs, DxgiCapture::Outputs());

    Eigen::Vector2f origin = Eigen::Vector2f::Constant(std::numeric_limits<float>::max());
    Eigen::Vector2f end = Eigen::Vector2f::Constant(std::numeric_limits<float>::lowest());
    std::vector<ScreenOverlay> screens;
    for (auto& output : outputs) {
        const Eigen::Vector2f position(static_cast<float>(output.x), static_cast<float>(output.y));
        const Eigen::Vector2f size(static_cast<float>(output.width), static_cast<float>(output.height));
        origin = origin.cwiseMin(position);
        end = end.cwiseMax(position + size);

        LOG_INFO("{}: Init Windows screen of res ({}, {}) at ({}, {})", output.name, output.width, output.height, output.x, output.y);

        auto state = CreateState(output.name, {output.width, output.height}, output.transform);
        ScreenMeta meta{.name = output.name, .id = state.Id(), .native_handle = 0};
        auto backend = std::make_unique<SplitOverlayBackend>(
            std::make_unique<ScreenRenderer>(output.name, std::make_unique<DxgiCapture>(output.name, std::move(output.device))),
            std::make_unique<ScreenInteraction>(position, size, output.transform)
        );
        screens.push_back({
            .meta = std::move(meta),
            .state = std::move(state),
            .backend = std::move(backend),
            .desktop_position = {output.x, output.y},
        });
    }

    g_hid->SetDesktopExtent(end - origin);
    g_hid->SetDesktopOrigin(origin);
    return screens;
}

#endif

#if FRAMEXR_FEATURE_X11

auto Screens::CreateXshm() -> Result<std::vector<ScreenOverlay>> {
    ASSIGN_OR_RETURN(auto monitors, Xshm::Monitors());

    Eigen::Vector2f extent = Eigen::Vector2f::Zero();
    std::vector<ScreenOverlay> screens;
    for (auto& monitor : monitors) {
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

        auto state = CreateState(
            monitor->name,
            {static_cast<int32_t>(monitor->width), static_cast<int32_t>(monitor->height)},
            FrameTransform::Normal
        );
        ScreenMeta meta{.name = monitor->name, .id = state.Id(), .native_handle = 0};
        const auto name = monitor->name;
        const Eigen::Vector2i desktop_position(monitor->x, monitor->y);
        auto backend = std::make_unique<SplitOverlayBackend>(
            std::make_unique<ScreenRenderer>(name, std::make_unique<XshmCapture>(std::move(monitor))),
            std::make_unique<ScreenInteraction>(position, size, FrameTransform::Normal)
        );
        screens.push_back({
            .meta = std::move(meta),
            .state = std::move(state),
            .backend = std::move(backend),
            .desktop_position = desktop_position,
        });
    }

    g_hid->SetDesktopExtent(extent);
    g_hid->SetDesktopOrigin(Eigen::Vector2f::Zero());
    return screens;
}

#else

auto Screens::CreateXshm() -> Result<std::vector<ScreenOverlay>> {
    return Unexpected("X11 support not enabled");
}

#endif
