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

#include "OverlayContainer.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <utility>

#include "Hid.hpp"
#include "Keyboard.hpp"
#include "Keymap.hpp"
#include "Logger.hpp"
#include "Screen.hpp"
#include "State.hpp"
#include "Watch.hpp"
#include "framexr_config.h"

#if FRAMEXR_FEATURE_WAYLAND
#include "WaylandClient.hpp"
#endif

namespace {

auto LoadKeymap(Result<Keymap> loaded) -> std::optional<Keymap> {
    if (!loaded) {
        LOG_WARN("Could not load keyboard layout: {}", loaded.error().Message());
        return std::nullopt;
    }
    return std::move(*loaded);
}

} // namespace

OverlayContainer::OverlayContainer() = default;

OverlayContainer::~OverlayContainer() = default;

auto OverlayContainer::CreateScreens(AppState& app, std::optional<Keymap>& keymap) -> Result<std::vector<ScreenOverlay>> {
#if FRAMEXR_FEATURE_WAYLAND
    wayland_ = WaylandClient::Connect();
    if (wayland_) {
        keymap = LoadKeymap(Keymap::FromWayland());
        return Screens::CreateWayland(*wayland_, app);
    }
#endif
    keymap = LoadKeymap(Keymap::FromX11());
    if (auto created = Screens::CreateX11Pipewire(app)) {
        return std::move(*created);
    } else {
        LOG_INFO("Will not use PipeWire capture: {}", created.error().Message());
    }
    return Screens::CreateXshm(app);
}

auto OverlayContainer::Create(AppState& app) -> Result<std::unique_ptr<OverlayContainer>> {
    std::unique_ptr<OverlayContainer> container(new OverlayContainer());
    app.screens.clear();
    std::optional<Keymap> keymap;
    ASSIGN_OR_RETURN(auto screens, container->CreateScreens(app, keymap));

    auto show_screens = app.config.show_screens;
    if (show_screens.empty() && !screens.empty()) {
        show_screens.push_back(screens.front().state.name);
    }

    for (auto& screen : screens) {
        if (std::ranges::contains(show_screens, screen.state.name)) {
            screen.state.show_hide = true;
        }
        container->Add(std::make_unique<Overlay>(std::move(screen.state), std::move(screen.backend)));
        app.screens.push_back(std::move(screen.meta));
    }

    ASSIGN_OR_RETURN(auto watch, Watch::Create(app));
    watch->state.want_visible = true;
    container->Add(std::move(watch));

    ASSIGN_OR_RETURN(auto keyboard, Keyboard::Create(app, std::move(keymap)));
    keyboard->state.show_hide = std::ranges::contains(show_screens, kKeyboardName);
    keyboard->state.want_visible = false;
    container->Add(std::move(keyboard));

    return container;
}

auto OverlayContainer::CreateEmpty() -> std::unique_ptr<OverlayContainer> {
    return std::unique_ptr<OverlayContainer>(new OverlayContainer());
}

auto OverlayContainer::ById(OverlayId id) -> Overlay* {
    const auto it = std::ranges::find_if(overlays_, [id](const auto& overlay) -> bool { return overlay->state.id == id; });
    return it == overlays_.end() ? nullptr : it->get();
}

auto OverlayContainer::ByName(std::string_view name) -> Overlay* {
    const auto it = std::ranges::find_if(overlays_, [name](const auto& overlay) -> bool { return overlay->state.name == name; });
    return it == overlays_.end() ? nullptr : it->get();
}

auto OverlayContainer::BySelector(const OverlaySelector& selector) -> Overlay* {
    if (const auto* id = std::get_if<OverlayId>(&selector)) {
        return ById(*id);
    }
    return ByName(std::get<std::string>(selector));
}

auto OverlayContainer::Add(std::unique_ptr<Overlay> overlay) -> void {
    const auto id = overlay->state.id;
    const auto position = std::ranges::lower_bound(overlays_, id, {}, [](const auto& item) -> OverlayId { return item->state.id; });
    if (position != overlays_.end() && (*position)->state.id == id) {
        *position = std::move(overlay);
    } else {
        overlays_.insert(position, std::move(overlay));
    }
}

auto OverlayContainer::RemoveAt(size_t index) -> std::unique_ptr<Overlay> {
    auto removed = std::move(overlays_[index]);
    overlays_.erase(overlays_.begin() + static_cast<std::ptrdiff_t>(index));
    return removed;
}

auto OverlayContainer::Remove(const OverlaySelector& selector) -> std::unique_ptr<Overlay> {
    auto* overlay = BySelector(selector);
    if (overlay == nullptr) {
        return nullptr;
    }
    const auto it = std::ranges::find_if(overlays_, [overlay](const auto& item) -> bool { return item.get() == overlay; });
    return RemoveAt(static_cast<size_t>(it - overlays_.begin()));
}

auto OverlayContainer::Update(AppState& app) -> Result<std::vector<std::unique_ptr<Overlay>>> {
    std::vector<std::unique_ptr<Overlay>> removed;
#if FRAMEXR_FEATURE_WAYLAND
    if (!wayland_) {
        return removed;
    }
    auto& wayland = *wayland_;
    wayland.DispatchPending();

    bool create_ran = false;
    bool extent_dirty = false;
    bool watch_dirty = false;
    std::optional<std::map<std::string, std::string>> token_store;

    const auto find_screen = [&app](uint32_t handle) -> std::vector<ScreenMeta>::iterator {
        return std::ranges::find_if(app.screens, [handle](const ScreenMeta& screen) -> bool {
            return screen.native_handle == handle;
        });
    };

    for (const auto& event : wayland.TakeEvents()) {
        switch (event.change) {
        case OutputChange::Create: {
            if (create_ran) {
                break;
            }
            auto screens = Screens::CreateWayland(wayland, app);
            create_ran = true;
            for (auto& screen : screens) {
                Add(std::make_unique<Overlay>(std::move(screen.state), std::move(screen.backend)));
                app.screens.push_back(std::move(screen.meta));
                watch_dirty = true;
            }
            break;
        }
        case OutputChange::Destroy: {
            const auto screen = find_screen(event.id);
            if (screen == app.screens.end()) {
                break;
            }
            auto overlay = Remove(screen->id);
            if (!overlay) {
                FatalError(std::format("{}: overlay missing for destroyed output", screen->name));
            }
            removed.push_back(std::move(overlay));
            LOG_INFO("{}: Destroyed", screen->name);
            app.screens.erase(screen);
            watch_dirty = true;
            extent_dirty = true;
            break;
        }
        case OutputChange::Logical: {
            const auto screen = find_screen(event.id);
            if (screen == app.screens.end()) {
                break;
            }
            const auto* output = wayland.FindOutput(event.id);
            auto* overlay = ById(screen->id);
            if (output == nullptr || overlay == nullptr) {
                break;
            }
            overlay->backend->SetInteraction(
                std::make_unique<ScreenInteraction>(
                    output->logical_position.cast<float>(),
                    output->logical_size.cast<float>(),
                    WaylandClient::ToFrameTransform(output->transform)
                )
            );
            extent_dirty = true;
            break;
        }
        case OutputChange::Physical: {
            const auto screen = find_screen(event.id);
            if (screen == app.screens.end()) {
                break;
            }
            const auto* output = wayland.FindOutput(event.id);
            auto* overlay = ById(screen->id);
            if (output == nullptr || overlay == nullptr) {
                break;
            }
            if (!token_store) {
                auto loaded = Screens::LoadTokens();
                if (!loaded) {
                    LOG_WARN("Failed to load PipeWire token config: {}", loaded.error().Message());
                }
                token_store = loaded.value_or(std::map<std::string, std::string>{});
            }
            if (auto renderer = Screens::CreateWaylandRenderer(
                    *output,
                    wayland.DmabufManager() != nullptr,
                    wayland.ScreencopyManager() != nullptr,
                    *token_store,
                    app
                )) {
                overlay->backend->SetRenderer(std::move(renderer));
            }
            extent_dirty = true;
            break;
        }
        }
    }

    if (extent_dirty && !create_ran) {
        app.hid->SetDesktopExtent(wayland.DesktopExtent().cast<float>());
        app.hid->SetDesktopOrigin(wayland.DesktopOrigin().cast<float>());
    }

    if (watch_dirty) {
        auto* watch = ByName(kWatchName);
        if (watch == nullptr) {
            FatalError("Watch overlay missing");
        }
        if (const auto result = Watch::RebuildCanvas(app, *watch); !result) {
            LOG_ERROR("Failed to create watch canvas: {}", result.error().Message());
        }
    }
#else
    static_cast<void>(app);
#endif
    return removed;
}

auto OverlayContainer::ShowHide(AppState& app) -> void {
    const bool any_shown = std::ranges::any_of(overlays_, [](const auto& overlay) -> bool {
        return overlay->state.show_hide && overlay->state.want_visible;
    });

    for (auto& overlay : overlays_) {
        auto& state = overlay->state;
        if (state.show_hide) {
            state.want_visible = !any_shown;
            if (state.want_visible && state.recenter) {
                state.Reset(app, false);
            }
        }
        if (!any_shown && state.name == kWatchName) {
            state.Reset(app, true);
        }
    }
}
