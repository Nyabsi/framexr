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

#include "Container.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <map>
#include <numbers>
#include <set>
#include <utility>

#include "Config.hpp"
#include "ConfigStore.hpp"
#include "Editor.hpp"
#include "HidProvider.hpp"
#include "Interaction.hpp"
#include "Keyboard.hpp"
#include "Keymap.hpp"
#include "Logger.hpp"
#include "Desktop.hpp"
#include "ScreenInteraction.hpp"
#include "ScreenRenderer.hpp"
#include "Watch.hpp"
#include "framexr_config.h"

#if FRAMEXR_FEATURE_WAYLAND
#include "WaylandClient.hpp"
#endif

namespace {
constexpr float kOverlapGap = 0.0175f;
constexpr int kMaxNudges = 16;

auto LoadKeymap(Result<Keymap> loaded) -> std::optional<Keymap> {
    if (!loaded) {
        LOG_WARN("Could not load keyboard layout: {}", loaded.error().Message());
        return std::nullopt;
    }
    return std::move(*loaded);
}

} // namespace

Container* g_container = nullptr;

Container::Container() {
    g_container = this;
}

Container::~Container() {
    if (g_container == this) {
        g_container = nullptr;
    }
}

auto Container::CreateDesktops(std::optional<Keymap>& keymap) -> Result<std::vector<std::unique_ptr<Desktop>>> {
#ifdef _WIN32
    keymap = LoadKeymap(Keymap::FromWindows());
    return Desktop::CreateWindows();
#else
#if FRAMEXR_FEATURE_WAYLAND
    m_wayland = WaylandClient::Connect();
    if (m_wayland) {
        keymap = LoadKeymap(Keymap::FromWayland());
        return Desktop::CreateWayland(*m_wayland, {});
    }
#endif
    keymap = LoadKeymap(Keymap::FromX11());
    if (auto created = Desktop::CreateX11Pipewire()) {
        return std::move(*created);
    } else {
        LOG_INFO("Will not use PipeWire capture: {}", created.error().Message());
    }
    return Desktop::CreateXshm();
#endif
}

auto Container::Create() -> Result<std::unique_ptr<Container>> {
    std::unique_ptr<Container> container(new Container());
    std::optional<Keymap> keymap;
    ASSIGN_OR_RETURN(auto desktops, container->CreateDesktops(keymap));
    Desktop::ArrangeInFrontOfHead(desktops);

    auto show_screens = g_config->show_screens;
    if (show_screens.empty() && !desktops.empty()) {
        show_screens.push_back(desktops.front()->Name());
    }
    for (auto& desktop : desktops) {
        desktop->SetShowHide(std::ranges::contains(show_screens, desktop->Name()));
        container->Add(std::move(desktop));
    }

    ASSIGN_OR_RETURN(auto watch, Watch::Create());
    watch->SetWantVisible(true);
    container->Add(std::move(watch));

    if (keymap) {
        g_interaction->SetTypingKeymap(std::make_shared<const Keymap>(std::move(*keymap)));
    }
    ASSIGN_OR_RETURN(auto keyboard, Keyboard::Create(g_interaction->TypingKeymap()));
    keyboard->SetShowHide(std::ranges::contains(show_screens, kKeyboardName));
    keyboard->SetWantVisible(false);
    container->Add(std::move(keyboard));

    return container;
}

auto Container::Desktops() const -> std::vector<Desktop*> {
    std::vector<Desktop*> desktops;
    for (const auto& view : m_views) {
        if (auto* desktop = dynamic_cast<Desktop*>(view.get())) {
            desktops.push_back(desktop);
        }
    }
    return desktops;
}

auto Container::CreateEmpty() -> std::unique_ptr<Container> {
    return std::unique_ptr<Container>(new Container());
}

auto Container::ById(OverlayId id) -> View* {
    const auto it = std::ranges::find_if(m_views, [id](const auto& overlay) -> bool { return overlay->Id() == id; });
    return it == m_views.end() ? nullptr : it->get();
}

auto Container::ByName(std::string_view name) -> View* {
    const auto it = std::ranges::find_if(m_views, [name](const auto& overlay) -> bool { return overlay->Name() == name; });
    return it == m_views.end() ? nullptr : it->get();
}

auto Container::BySelector(const OverlaySelector& selector) -> View* {
    if (const auto* id = std::get_if<OverlayId>(&selector)) {
        return ById(*id);
    }
    return ByName(std::get<std::string>(selector));
}

auto Container::Add(std::unique_ptr<View> overlay) -> void {
    const auto id = overlay->Id();
    const auto position = std::ranges::lower_bound(m_views, id, {}, [](const auto& item) -> OverlayId { return item->Id(); });
    if (position != m_views.end() && (*position)->Id() == id) {
        *position = std::move(overlay);
    } else {
        m_views.insert(position, std::move(overlay));
    }
}

auto Container::RemoveAt(size_t index) -> std::unique_ptr<View> {
    auto removed = std::move(m_views[index]);
    m_views.erase(m_views.begin() + static_cast<std::ptrdiff_t>(index));
    return removed;
}

auto Container::Remove(const OverlaySelector& selector) -> std::unique_ptr<View> {
    auto* overlay = BySelector(selector);
    if (overlay == nullptr) {
        return nullptr;
    }
    const auto it = std::ranges::find_if(m_views, [overlay](const auto& item) -> bool { return item.get() == overlay; });
    return RemoveAt(static_cast<size_t>(it - m_views.begin()));
}

auto Container::Update() -> Result<std::vector<std::unique_ptr<View>>> {
    std::vector<std::unique_ptr<View>> removed;
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

    const auto find_desktop = [this](uint32_t output) -> Desktop* {
        const auto desktops = Desktops();
        const auto found = std::ranges::find(desktops, output, &Desktop::Output);
        return found == desktops.end() ? nullptr : *found;
    };

    for (const auto& event : wayland.TakeEvents()) {
        switch (event.change) {
        case OutputChange::Create: {
            if (create_ran) {
                break;
            }
            create_ran = true;
            for (auto& desktop : Desktop::CreateWayland(wayland, Desktops())) {
                Add(std::move(desktop));
                watch_dirty = true;
            }
            break;
        }
        case OutputChange::Destroy: {
            auto* desktop = find_desktop(event.id);
            if (desktop == nullptr) {
                break;
            }
            LOG_INFO("{}: Destroyed", desktop->Name());
            removed.push_back(Remove(desktop->Id()));
            watch_dirty = true;
            extent_dirty = true;
            break;
        }
        case OutputChange::Logical: {
            auto* desktop = find_desktop(event.id);
            const auto* output = wayland.FindOutput(event.id);
            if (output == nullptr || desktop == nullptr) {
                break;
            }
            desktop->SetInput(ScreenInteraction(
                output->logical_position.cast<float>(),
                output->logical_size.cast<float>(),
                WaylandClient::ToFrameTransform(output->transform)
            ));
            extent_dirty = true;
            break;
        }
        case OutputChange::Physical: {
            auto* desktop = find_desktop(event.id);
            const auto* output = wayland.FindOutput(event.id);
            if (output == nullptr || desktop == nullptr) {
                break;
            }
            if (!token_store) {
                auto loaded = ConfigStore::PipewireTokens();
                if (!loaded) {
                    LOG_WARN("Failed to load PipeWire token config: {}", loaded.error().Message());
                }
                token_store = loaded.value_or(std::map<std::string, std::string>{});
            }
            if (auto renderer = Desktop::CreateCompose(
                    *output,
                    wayland.DmabufManager() != nullptr,
                    wayland.ScreencopyManager() != nullptr,
                    *token_store
                )) {
                desktop->SetComposer(std::make_unique<Compose>(std::move(renderer)));
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
        if (const auto result = dynamic_cast<Watch&>(*watch).Rebuild(); !result) {
            LOG_ERROR("Failed to create watch canvas: {}", result.error().Message());
        }
    }
#endif
    return removed;
}

auto Container::ShowHide() -> void {
    const auto hideable = [](const View& state) -> bool {
        return state.ShowHide() || (state.Recenter() && state.AttachedTo() == RelativeTo::None);
    };
    const bool editor_shown = m_editor && !m_editor->Closing() && !m_editor->Hidden();
    const bool any_shown = editor_shown || std::ranges::any_of(m_views, [&](const auto& overlay) -> bool {
                               return hideable(*overlay) && overlay->WantVisible();
                           });

    if (any_shown) {
        if (m_editor) {
            m_editor->SetHidden(true);
        }
        m_hidden_by_show_hide.clear();
        for (auto& overlay : m_views) {
            auto& state = *overlay;
            if (hideable(state) && state.WantVisible()) {
                m_hidden_by_show_hide.push_back(state.Id());
                state.SetWantVisible(false);
            }
        }
        return;
    }

    const auto restore = [&](const View& state) -> bool {
        return m_hidden_by_show_hide.empty() ? state.ShowHide() : std::ranges::contains(m_hidden_by_show_hide, state.Id());
    };
    for (auto& overlay : m_views) {
        auto& state = *overlay;
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
        m_editor->Placement().Recenter();
    }
    ArrangeVisible();
}

auto Container::Recenter() -> void {
    if (m_editor) {
        m_editor->Placement().Recenter();
    }
    for (auto& overlay : m_views) {
        auto& state = *overlay;
        if (state.WantVisible() && state.Recenter() && state.AttachedTo() == RelativeTo::None) {
            state.Reset(false);
        }
    }
    ArrangeVisible();
}

auto Container::LockedVisible() -> std::vector<View*> {
    std::vector<View*> locked;
    for (auto& overlay : m_views) {
        if (overlay->WantVisible() && !overlay->Recenter() && overlay->AttachedTo() == RelativeTo::None) {
            locked.push_back(overlay.get());
        }
    }
    return locked;
}

auto Container::FreeVisible() -> std::vector<View*> {
    const auto rank = [](const View& view) -> int {
        if (dynamic_cast<const Desktop*>(&view) != nullptr) {
            return 0;
        }
        return view.Name() == kKeyboardName ? 1 : 2;
    };

    std::vector<View*> visible;
    for (auto& overlay : m_views) {
        if (overlay->WantVisible() && overlay->Recenter() && overlay->AttachedTo() == RelativeTo::None) {
            visible.push_back(overlay.get());
        }
    }
    std::ranges::stable_sort(visible, [&rank](const View* a, const View* b) -> bool { return rank(*a) < rank(*b); });
    return visible;
}

auto Container::RememberUserPlacement(View& placed) -> void {
    std::erase_if(m_user_overlaps, [&](const OverlayPair& pair) -> bool {
        return pair.first == placed.Id() || pair.second == placed.Id();
    });
    const auto corners = Corners(placed);
    if (!corners) {
        return;
    }
    const ViewRect bounds = ViewBounds(*corners, g_interaction->Hmd());

    auto others = LockedVisible();
    std::ranges::copy(FreeVisible(), std::back_inserter(others));
    for (auto* other : others) {
        if (other == &placed) {
            continue;
        }
        if (const auto other_corners = Corners(*other);
            other_corners && Overlaps(bounds, ViewBounds(*other_corners, g_interaction->Hmd()), 0.0f)) {
            m_user_overlaps.insert(std::minmax(placed.Id(), other->Id()));
        }
    }
}

auto Container::PlaceShown(View& shown) -> void {
    if (!shown.Recenter() || shown.AttachedTo() != RelativeTo::None) {
        return;
    }
    auto others = LockedVisible();
    std::ranges::copy(FreeVisible(), std::back_inserter(others));
    std::erase_if(others, [&](const View* other) -> bool {
        return other == &shown || m_user_overlaps.contains(std::minmax(shown.Id(), other->Id()));
    });
    NudgeClear(shown, others, g_interaction->Hmd());
}

auto Container::UpdateTexelFootprints() -> void {
    if (g_interaction->DisplayPixelsPerRadian().minCoeff() <= 0.0f) {
        return;
    }
    for (auto& overlay : m_views) {
        if (!overlay->WantVisible()) {
            continue;
        }
        if (const auto footprint = MeasureFootprint(*overlay, g_interaction->Hmd(), g_interaction->DisplayPixelsPerRadian())) {
            overlay->Composer()->SetTexelFootprint(*footprint);
        }
    }
}

auto Container::OpenEditor() -> void {
    if (m_editor && !m_editor->Closing()) {
        m_editor->Suspend();
        return;
    }
    m_editor = Editor::Create();
    m_editor->Open();
}

auto Container::UpdateEditor() -> void {
    if (!m_editor) {
        return;
    }
    m_editor->Update();
    if (m_editor->Finished()) {
        m_editor.reset();
    }
}

auto Container::EditorLines() -> std::vector<EditorLine> {
    return m_editor ? m_editor->Lines() : std::vector<EditorLine>{};
}

auto Container::ArrangeVisible() -> void {
    auto placed = LockedVisible();
    for (auto* overlay : FreeVisible()) {
        std::vector<View*> obstacles;
        std::ranges::copy_if(placed, std::back_inserter(obstacles), [&](const View* other) -> bool {
            return !m_user_overlaps.contains(std::minmax(overlay->Id(), other->Id()));
        });
        NudgeClear(*overlay, obstacles, g_interaction->Hmd());
        placed.push_back(overlay);
    }
}

auto Container::Corners(View& overlay) -> std::optional<std::array<Eigen::Vector3f, 4>> {
    const auto meta = overlay.Composer()->Meta();
    const VkExtent2D extent = meta && meta->extent.width > 0 && meta->extent.height > 0 ? meta->extent : overlay.ExtentHint();
    if (extent.width == 0 || extent.height == 0) {
        return std::nullopt;
    }
    const Eigen::Matrix4f frame = meta ? meta->transform.matrix() : Eigen::Matrix4f::Identity();
    const Eigen::Matrix4f effective = overlay.Transform().matrix() * frame;

    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const float half_width = std::min(aspect, 1.0f) / 2.0f;
    const float half_height = half_width / aspect;

    float edge = half_width;
    float bulge = 0.0f;
    if (const auto curvature = overlay.Curvature(); curvature && *curvature > 0.0f) {
        const float angle = std::numbers::pi_v<float> * *curvature;
        const float radius = half_width / angle;
        edge = radius * std::sin(angle);
        bulge = radius * (1.0f - std::cos(angle));
    }

    std::array<Eigen::Vector3f, 4> corners;
    const std::array<Eigen::Vector2f, 4> local{{
        {-edge, -half_height},
        {edge, -half_height},
        {edge, half_height},
        {-edge, half_height},
    }};
    for (size_t i = 0; i < corners.size(); ++i) {
        const Eigen::Vector4f point = effective * Eigen::Vector4f(local[i].x(), local[i].y(), bulge, 1.0f);
        corners[i] = point.head<3>();
    }
    return corners;
}

auto Container::ViewBounds(const std::array<Eigen::Vector3f, 4>& corners, const Eigen::Affine3f& head) -> ViewRect {
    const Eigen::Matrix3f to_head = Eigen::Matrix3f(head.linear()).transpose();
    const Eigen::Vector3f origin = head.translation();
    ViewRect rect{
        .min_azimuth = std::numeric_limits<float>::max(),
        .max_azimuth = std::numeric_limits<float>::lowest(),
        .min_elevation = std::numeric_limits<float>::max(),
        .max_elevation = std::numeric_limits<float>::lowest(),
    };
    for (const auto& corner : corners) {
        const Eigen::Vector3f local = to_head * (corner - origin);
        const float azimuth = std::atan2(local.x(), -local.z());
        const float elevation = std::atan2(local.y(), std::hypot(local.x(), local.z()));
        rect.min_azimuth = std::min(rect.min_azimuth, azimuth);
        rect.max_azimuth = std::max(rect.max_azimuth, azimuth);
        rect.min_elevation = std::min(rect.min_elevation, elevation);
        rect.max_elevation = std::max(rect.max_elevation, elevation);
    }
    return rect;
}

auto Container::Overlaps(const ViewRect& a, const ViewRect& b, float gap) -> bool {
    return a.min_azimuth < b.max_azimuth + gap && b.min_azimuth < a.max_azimuth + gap && a.min_elevation < b.max_elevation + gap
        && b.min_elevation < a.max_elevation + gap;
}

auto Container::ArcPerChord(const std::optional<float>& curvature) -> float {
    if (!curvature || *curvature <= 0.0f) {
        return 1.0f;
    }
    const float angle = std::numbers::pi_v<float> * *curvature;
    return angle / std::sin(angle);
}

auto Container::MeasureFootprint(View& overlay, const Eigen::Affine3f& head, const Eigen::Vector2f& display_pixels_per_radian)
    -> std::optional<TexelFootprint> {
    const auto corners = Corners(overlay);
    if (!corners || display_pixels_per_radian.minCoeff() <= 0.0f) {
        return std::nullopt;
    }
    const Eigen::Vector3f across = (*corners)[1] - (*corners)[0];
    const Eigen::Vector3f up = (*corners)[3] - (*corners)[0];
    if (across.norm() <= 0.0f || up.norm() <= 0.0f) {
        return std::nullopt;
    }
    const Eigen::Vector3f across_unit = across.normalized();
    const Eigen::Vector3f up_unit = up.normalized();
    const Eigen::Vector3f normal = across_unit.cross(up_unit);
    const Eigen::Vector3f center = ((*corners)[0] + (*corners)[2]) * 0.5f;
    const Eigen::Vector3f eye = head.translation() - center;
    return TexelFootprint{
        .eye = Eigen::Vector3f(eye.dot(across_unit), eye.dot(up_unit), eye.dot(normal)),
        .size = Eigen::Vector2f(across.norm() * ArcPerChord(overlay.Curvature()), up.norm()),
        .display_pixels_per_radian = display_pixels_per_radian,
    };
}

auto Container::RotateAroundHead(View& overlay, const Eigen::Affine3f& head, const Eigen::Vector3f& axis, float angle) -> void {
    const Eigen::Matrix3f rotation = Eigen::AngleAxisf(angle, axis).toRotationMatrix();
    const Eigen::Vector3f pivot = head.translation();
    Eigen::Matrix4f around = Eigen::Matrix4f::Identity();
    around.topLeftCorner<3, 3>() = rotation;
    around.topRightCorner<3, 1>() = pivot - rotation * pivot;
    overlay.SetTransform(Eigen::Affine3f(around * overlay.Transform().matrix()));
    overlay.SetDirty(true);
}

auto Container::NudgeClear(View& moving, std::span<View* const> fixed, const Eigen::Affine3f& head) -> bool {
    const Eigen::Matrix3f head_rotation = head.linear();
    const Eigen::Vector3f up = head_rotation.col(1);
    const Eigen::Vector3f right = head_rotation.col(0);

    bool moved = false;
    for (int attempt = 0; attempt < kMaxNudges; ++attempt) {
        const auto moving_corners = Corners(moving);
        if (!moving_corners) {
            return moved;
        }
        const ViewRect rect = ViewBounds(*moving_corners, head);

        std::optional<ViewRect> blocker;
        for (auto* other : fixed) {
            if (other == &moving) {
                continue;
            }
            if (const auto corners = Corners(*other)) {
                if (const ViewRect bounds = ViewBounds(*corners, head); Overlaps(rect, bounds, kOverlapGap)) {
                    blocker = bounds;
                    break;
                }
            }
        }
        if (!blocker) {
            return moved;
        }

        const float left = blocker->min_azimuth - kOverlapGap - rect.max_azimuth;
        const float rightward = blocker->max_azimuth + kOverlapGap - rect.min_azimuth;
        const float down = blocker->min_elevation - kOverlapGap - rect.max_elevation;
        const float upward = blocker->max_elevation + kOverlapGap - rect.min_elevation;

        const float horizontal = std::abs(left) < std::abs(rightward) ? left : rightward;
        const float vertical = std::abs(down) < std::abs(upward) ? down : upward;
        if (std::abs(vertical) <= std::abs(horizontal)) {
            RotateAroundHead(moving, head, right, vertical);
        } else {
            RotateAroundHead(moving, head, up, -horizontal);
        }
        moved = true;
    }
    return moved;
}
