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
#include <iterator>
#include <map>
#include <set>
#include <utility>

#include "Config.hpp"
#include "Editor.hpp"
#include "Hid.hpp"
#include "Input.hpp"
#include "Keyboard.hpp"
#include "Keymap.hpp"
#include "Logger.hpp"
#include "OverlayLayout.hpp"
#include "Screen.hpp"
#include "ScreenInteraction.hpp"
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

OverlayContainer* g_overlays = nullptr;

OverlayContainer::OverlayContainer() {
    g_overlays = this;
}

OverlayContainer::~OverlayContainer() {
    if (g_overlays == this) {
        g_overlays = nullptr;
    }
}

auto OverlayContainer::CreateScreens(std::optional<Keymap>& keymap) -> Result<std::vector<ScreenOverlay>> {
#ifdef _WIN32
    keymap = LoadKeymap(Keymap::FromWindows());
    return Screens::CreateWindows();
#else
#if FRAMEXR_FEATURE_WAYLAND
    m_wayland = WaylandClient::Connect();
    if (m_wayland) {
        keymap = LoadKeymap(Keymap::FromWayland());
        return Screens::CreateWayland(*m_wayland);
    }
#endif
    keymap = LoadKeymap(Keymap::FromX11());
    if (auto created = Screens::CreateX11Pipewire()) {
        return std::move(*created);
    } else {
        LOG_INFO("Will not use PipeWire capture: {}", created.error().Message());
    }
    return Screens::CreateXshm();
#endif
}

auto OverlayContainer::Create() -> Result<std::unique_ptr<OverlayContainer>> {
    std::unique_ptr<OverlayContainer> container(new OverlayContainer());
    std::optional<Keymap> keymap;
    ASSIGN_OR_RETURN(auto screens, container->CreateScreens(keymap));
    Screens::ArrangeInFrontOfHead(screens);

    auto show_screens = g_config->show_screens;
    if (show_screens.empty() && !screens.empty()) {
        show_screens.push_back(screens.front().state.Name());
    }

    for (auto& screen : screens) {
        if (std::ranges::contains(show_screens, screen.state.Name())) {
            screen.state.SetShowHide(true);
        }
        container->Add(std::make_unique<Overlay>(std::move(screen.state), std::move(screen.backend)));
        container->m_screens.push_back(std::move(screen.meta));
    }

    ASSIGN_OR_RETURN(auto watch, Watch::Create());
    watch->State().SetWantVisible(true);
    container->Add(std::move(watch));

    if (keymap) {
        g_input->SetTypingKeymap(std::make_shared<const Keymap>(std::move(*keymap)));
    }
    ASSIGN_OR_RETURN(auto keyboard, Keyboard::Create(g_input->TypingKeymap()));
    keyboard->State().SetShowHide(std::ranges::contains(show_screens, kKeyboardName));
    keyboard->State().SetWantVisible(false);
    container->Add(std::move(keyboard));

    return container;
}

auto OverlayContainer::CreateEmpty() -> std::unique_ptr<OverlayContainer> {
    return std::unique_ptr<OverlayContainer>(new OverlayContainer());
}

auto OverlayContainer::ById(OverlayId id) -> Overlay* {
    const auto it = std::ranges::find_if(m_overlays, [id](const auto& overlay) -> bool { return overlay->State().Id() == id; });
    return it == m_overlays.end() ? nullptr : it->get();
}

auto OverlayContainer::ByName(std::string_view name) -> Overlay* {
    const auto it = std::ranges::find_if(m_overlays, [name](const auto& overlay) -> bool {
        return overlay->State().Name() == name;
    });
    return it == m_overlays.end() ? nullptr : it->get();
}

auto OverlayContainer::BySelector(const OverlaySelector& selector) -> Overlay* {
    if (const auto* id = std::get_if<OverlayId>(&selector)) {
        return ById(*id);
    }
    return ByName(std::get<std::string>(selector));
}

auto OverlayContainer::Add(std::unique_ptr<Overlay> overlay) -> void {
    const auto id = overlay->State().Id();
    const auto position = std::ranges::lower_bound(m_overlays, id, {}, [](const auto& item) -> OverlayId {
        return item->State().Id();
    });
    if (position != m_overlays.end() && (*position)->State().Id() == id) {
        *position = std::move(overlay);
    } else {
        m_overlays.insert(position, std::move(overlay));
    }
}

auto OverlayContainer::RemoveAt(size_t index) -> std::unique_ptr<Overlay> {
    auto removed = std::move(m_overlays[index]);
    m_overlays.erase(m_overlays.begin() + static_cast<std::ptrdiff_t>(index));
    return removed;
}

auto OverlayContainer::Remove(const OverlaySelector& selector) -> std::unique_ptr<Overlay> {
    auto* overlay = BySelector(selector);
    if (overlay == nullptr) {
        return nullptr;
    }
    const auto it = std::ranges::find_if(m_overlays, [overlay](const auto& item) -> bool { return item.get() == overlay; });
    return RemoveAt(static_cast<size_t>(it - m_overlays.begin()));
}

auto OverlayContainer::Update() -> Result<std::vector<std::unique_ptr<Overlay>>> {
    std::vector<std::unique_ptr<Overlay>> removed;
#if FRAMEXR_FEATURE_WAYLAND
    if (!m_wayland) {
        return removed;
    }
    auto& wayland = *m_wayland;
    wayland.DispatchPending();

    bool create_ran = false;
    bool extent_dirty = false;
    bool watch_dirty = false;
    std::optional<std::map<std::string, std::string>> token_store;

    const auto find_screen = [this](uint32_t handle) -> std::vector<ScreenMeta>::iterator {
        return std::ranges::find_if(m_screens, [handle](const ScreenMeta& screen) -> bool {
            return screen.native_handle == handle;
        });
    };

    for (const auto& event : wayland.TakeEvents()) {
        switch (event.change) {
        case OutputChange::Create: {
            if (create_ran) {
                break;
            }
            auto screens = Screens::CreateWayland(wayland);
            create_ran = true;
            for (auto& screen : screens) {
                Add(std::make_unique<Overlay>(std::move(screen.state), std::move(screen.backend)));
                m_screens.push_back(std::move(screen.meta));
                watch_dirty = true;
            }
            break;
        }
        case OutputChange::Destroy: {
            const auto screen = find_screen(event.id);
            if (screen == m_screens.end()) {
                break;
            }
            auto overlay = Remove(screen->id);
            if (!overlay) {
                FatalError(std::format("{}: overlay missing for destroyed output", screen->name));
            }
            removed.push_back(std::move(overlay));
            LOG_INFO("{}: Destroyed", screen->name);
            m_screens.erase(screen);
            watch_dirty = true;
            extent_dirty = true;
            break;
        }
        case OutputChange::Logical: {
            const auto screen = find_screen(event.id);
            if (screen == m_screens.end()) {
                break;
            }
            const auto* output = wayland.FindOutput(event.id);
            auto* overlay = ById(screen->id);
            if (output == nullptr || overlay == nullptr) {
                break;
            }
            overlay->Backend()->SetInteraction(
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
            if (screen == m_screens.end()) {
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
                    *token_store
                )) {
                overlay->Backend()->SetRenderer(std::move(renderer));
            }
            extent_dirty = true;
            break;
        }
        }
    }

    if (extent_dirty && !create_ran) {
        g_hid->SetDesktopExtent(wayland.DesktopExtent().cast<float>());
        g_hid->SetDesktopOrigin(wayland.DesktopOrigin().cast<float>());
    }

    if (watch_dirty) {
        auto* watch = ByName(kWatchName);
        if (watch == nullptr) {
            FatalError("Watch overlay missing");
        }
        if (const auto result = Watch::RebuildCanvas(*watch); !result) {
            LOG_ERROR("Failed to create watch canvas: {}", result.error().Message());
        }
    }
#endif
    return removed;
}

auto OverlayContainer::ShowHide() -> void {
    const auto hideable = [](const OverlayState& state) -> bool {
        return state.ShowHide() || (state.Recenter() && state.AttachedTo() == RelativeTo::None);
    };
    const bool editor_shown = m_editor && !m_editor->Closing() && !m_editor->Hidden();
    const bool any_shown = editor_shown || std::ranges::any_of(m_overlays, [&](const auto& overlay) -> bool {
                               return hideable(overlay->State()) && overlay->State().WantVisible();
                           });

    if (any_shown) {
        if (m_editor) {
            m_editor->SetHidden(true);
        }
        m_hidden_by_show_hide.clear();
        for (auto& overlay : m_overlays) {
            auto& state = overlay->State();
            if (hideable(state) && state.WantVisible()) {
                m_hidden_by_show_hide.push_back(state.Id());
                state.SetWantVisible(false);
            }
        }
        return;
    }

    const auto restore = [&](const OverlayState& state) -> bool {
        return m_hidden_by_show_hide.empty() ? state.ShowHide() : std::ranges::contains(m_hidden_by_show_hide, state.Id());
    };
    for (auto& overlay : m_overlays) {
        auto& state = overlay->State();
        if (restore(state)) {
            state.SetWantVisible(true);
            if (state.Recenter()) {
                state.Reset(false);
            }
        }
        if (state.Name() == kWatchName) {
            state.Reset(true);
        }
    }
    m_hidden_by_show_hide.clear();
    if (m_editor && m_editor->Hidden()) {
        m_editor->SetHidden(false);
        m_editor->Recenter();
    }
    ArrangeVisible();
}

auto OverlayContainer::Recenter() -> void {
    if (m_editor) {
        m_editor->Recenter();
    }
    for (auto& overlay : m_overlays) {
        auto& state = overlay->State();
        if (state.WantVisible() && state.Recenter() && state.AttachedTo() == RelativeTo::None) {
            state.Reset(false);
        }
    }
    ArrangeVisible();
}

auto OverlayContainer::LockedVisible() -> std::vector<Overlay*> {
    std::vector<Overlay*> locked;
    for (auto& overlay : m_overlays) {
        if (overlay->State().WantVisible() && !overlay->State().Recenter() && overlay->State().AttachedTo() == RelativeTo::None) {
            locked.push_back(overlay.get());
        }
    }
    return locked;
}

auto OverlayContainer::FreeVisible() -> std::vector<Overlay*> {
    const auto rank = [this](const Overlay& overlay) -> int {
        if (std::ranges::any_of(m_screens, [&](const ScreenMeta& screen) -> bool { return screen.id == overlay.State().Id(); })) {
            return 0;
        }
        return overlay.State().Name() == kKeyboardName ? 1 : 2;
    };

    std::vector<Overlay*> visible;
    for (auto& overlay : m_overlays) {
        if (overlay->State().WantVisible() && overlay->State().Recenter() && overlay->State().AttachedTo() == RelativeTo::None) {
            visible.push_back(overlay.get());
        }
    }
    std::ranges::stable_sort(visible, [&rank](const Overlay* a, const Overlay* b) -> bool { return rank(*a) < rank(*b); });
    return visible;
}

auto OverlayContainer::RememberUserPlacement(Overlay& placed) -> void {
    std::erase_if(m_user_overlaps, [&](const OverlayPair& pair) -> bool {
        return pair.first == placed.State().Id() || pair.second == placed.State().Id();
    });
    const auto corners = OverlayLayout::Corners(placed);
    if (!corners) {
        return;
    }
    const ViewRect bounds = OverlayLayout::ViewBounds(*corners, g_input->Hmd());

    auto others = LockedVisible();
    std::ranges::copy(FreeVisible(), std::back_inserter(others));
    for (auto* other : others) {
        if (other == &placed) {
            continue;
        }
        if (const auto other_corners = OverlayLayout::Corners(*other);
            other_corners && OverlayLayout::Overlaps(bounds, OverlayLayout::ViewBounds(*other_corners, g_input->Hmd()), 0.0f)) {
            m_user_overlaps.insert(std::minmax(placed.State().Id(), other->State().Id()));
        }
    }
}

auto OverlayContainer::PlaceShown(Overlay& shown) -> void {
    if (!shown.State().Recenter() || shown.State().AttachedTo() != RelativeTo::None) {
        return;
    }
    auto others = LockedVisible();
    std::ranges::copy(FreeVisible(), std::back_inserter(others));
    std::erase_if(others, [&](const Overlay* other) -> bool {
        return other == &shown || m_user_overlaps.contains(std::minmax(shown.State().Id(), other->State().Id()));
    });
    OverlayLayout::NudgeClear(shown, others, g_input->Hmd());
}

auto OverlayContainer::UpdateTexelFootprints() -> void {
    if (g_input->DisplayPixelsPerRadian().minCoeff() <= 0.0f) {
        return;
    }
    for (auto& overlay : m_overlays) {
        if (!overlay->State().WantVisible()) {
            continue;
        }
        if (const auto footprint = OverlayLayout::MeasureFootprint(*overlay, g_input->Hmd(), g_input->DisplayPixelsPerRadian())) {
            overlay->Backend()->SetTexelFootprint(*footprint);
        }
    }
}

auto OverlayContainer::OpenEditor() -> void {
    if (m_editor && !m_editor->Closing()) {
        m_editor->Suspend();
        return;
    }
    m_editor = Editor::Create();
    m_editor->Open();
}

auto OverlayContainer::UpdateEditor() -> void {
    if (!m_editor) {
        return;
    }
    m_editor->Update();
    if (m_editor->Finished()) {
        m_editor.reset();
    }
}

auto OverlayContainer::EditorLines() -> std::vector<EditorLine> {
    return m_editor ? m_editor->Lines() : std::vector<EditorLine>{};
}

auto OverlayContainer::ArrangeVisible() -> void {
    auto placed = LockedVisible();
    for (auto* overlay : FreeVisible()) {
        std::vector<Overlay*> obstacles;
        std::ranges::copy_if(placed, std::back_inserter(obstacles), [&](const Overlay* other) -> bool {
            return !m_user_overlaps.contains(std::minmax(overlay->State().Id(), other->State().Id()));
        });
        OverlayLayout::NudgeClear(*overlay, obstacles, g_input->Hmd());
        placed.push_back(overlay);
    }
}
