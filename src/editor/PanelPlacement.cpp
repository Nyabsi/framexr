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

#include "PanelPlacement.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

#include "Config.hpp"
#include "Editor.hpp"
#include "Interaction.hpp"
#include "TaskQueue.hpp"
#include "views/EditorPanel.hpp"

namespace
{

constexpr float kPanelGap = 0.04f;
constexpr float kPreviewHeight = -0.08f;
constexpr float kGhostReach = 0.3f;
constexpr float kGhostWidth = 0.12f;
constexpr float kHandPaletteWidth = 0.2f;

constexpr std::array<std::string_view, 9> kPanelNames{
    "editor-preview",
    "editor-toolbar",
    "editor-palette",
    "editor-properties",
    "editor-dialog",
    "editor-ghost",
    "editor-layer-back",
    "editor-layer-middle",
    "editor-layer-front",
};

constexpr std::array kLayerPanels{PanelKind::BackLayer, PanelKind::MiddleLayer, PanelKind::FrontLayer};

auto PointOn(const std::array<Eigen::Vector3f, 4>& corners, const Eigen::Vector2f& uv) -> Eigen::Vector3f
{
    return corners[0] + (corners[1] - corners[0]) * uv.x() + (corners[3] - corners[0]) * (1.0f - uv.y());
}

auto LineBetween(const Eigen::Vector3f& from, const Eigen::Vector3f& to) -> EditorLine
{
    return {.pose = LaserPointer::LinePose(Eigen::Affine3f(Eigen::Translation3f(from)), to), .length = (to - from).norm()};
}

auto Shown(const Editor& editor, PanelKind kind) -> bool
{
    if (editor.Hidden())
    {
        return false;
    }
    switch (kind)
    {
    case PanelKind::Dialog: return editor.Dialog() != DialogKind::None;
    case PanelKind::Ghost: return editor.Carrying().has_value();
    case PanelKind::Preview: return !editor.Exploded();
    case PanelKind::BackLayer:
    case PanelKind::MiddleLayer:
    case PanelKind::FrontLayer: return editor.Exploded();
    case PanelKind::Toolbar:
    case PanelKind::Palette:
    case PanelKind::Properties: break;
    }
    return true;
}

} // namespace

auto PanelPlacement::PanelName(PanelKind kind) -> std::string_view
{
    return kPanelNames.at(static_cast<size_t>(kind));
}

auto PanelPlacement::LayerOf(PanelKind kind) -> std::optional<int>
{
    switch (kind)
    {
    case PanelKind::BackLayer: return 0;
    case PanelKind::MiddleLayer: return 1;
    case PanelKind::FrontLayer: return 2;
    case PanelKind::Preview:
    case PanelKind::Toolbar:
    case PanelKind::Palette:
    case PanelKind::Properties:
    case PanelKind::Dialog:
    case PanelKind::Ghost: break;
    }
    return std::nullopt;
}

auto PanelPlacement::LocalTransform(const Editor& editor, PanelKind kind) -> Eigen::Affine3f
{
    const auto meters = [&editor](PanelKind panel) -> Eigen::Vector2f { return editor.PanelSize(panel) * kMetersPerPixel; };
    const Eigen::Vector2f preview = meters(PanelKind::Preview);
    const Eigen::Vector2f size = meters(kind);
    float azimuth = 0.0f;
    float height = kPreviewHeight;
    float depth = 0.0f;
    switch (kind)
    {
    case PanelKind::Preview:
    case PanelKind::Ghost: break;
    case PanelKind::BackLayer:
    case PanelKind::MiddleLayer:
    case PanelKind::FrontLayer: depth = kLayerSpacing * static_cast<float>(LayerOf(kind).value_or(0)); break;
    case PanelKind::Toolbar: height += preview.y() / 2.0f + kPanelGap / 2.0f + size.y() / 2.0f; break;
    case PanelKind::Palette:
        azimuth = (preview.x() / 2.0f + kPanelGap + size.x() / 2.0f) / kRadius;
        height += preview.y() / 2.0f - size.y() / 2.0f;
        break;
    case PanelKind::Properties:
        azimuth = -(preview.x() / 2.0f + kPanelGap + size.x() / 2.0f) / kRadius;
        height += preview.y() / 2.0f - size.y() / 2.0f;
        break;
    case PanelKind::Dialog:
        azimuth = -(preview.x() / 2.0f + 2.0f * kPanelGap + meters(PanelKind::Properties).x() + size.x() / 2.0f) / kRadius;
        height += preview.y() / 2.0f - size.y() / 2.0f;
        break;
    }
    Eigen::Affine3f local(Eigen::AngleAxisf(azimuth, Eigen::Vector3f::UnitY()));
    local.translate(Eigen::Vector3f(0.0f, height, depth - kRadius));
    local.scale(size.maxCoeff());
    return local;
}

auto PanelPlacement::ShowsPreview(OverlayId id) const -> bool
{
    return id == m_preview_id || std::ranges::contains(m_layer_ids, std::optional(id));
}

auto PanelPlacement::Recenter() -> void
{
    const Eigen::Matrix3f head = g_interaction->Hmd().linear();
    Eigen::Vector3f forward = head * -Eigen::Vector3f::UnitZ();
    forward.y() = 0.0f;
    const float yaw = forward.norm() > 1e-4f ? std::atan2(-forward.x(), -forward.z()) : 0.0f;
    m_anchor = Eigen::Translation3f(g_interaction->Hmd().translation()) * Eigen::AngleAxisf(yaw, Eigen::Vector3f::UnitY());
}

auto PanelPlacement::Open(const std::shared_ptr<Editor>& editor) -> void
{
    Recenter();
    std::weak_ptr<Editor> weak = editor;
    for (const auto kind : kPanels)
    {
        g_tasks->Enqueue(
            CreateOverlayTask{
                .selector = std::string(PanelName(kind)),
                .create = [weak, kind]() -> std::unique_ptr<View>
                {
                    const auto locked = weak.lock();
                    return locked ? std::make_unique<EditorPanel>(locked, kind) : nullptr;
                },
            }
        );
    }
}

auto PanelPlacement::Close() -> void
{
    for (const auto kind : kPanels)
    {
        g_tasks->Enqueue(DropOverlayTask{.selector = std::string(PanelName(kind))});
    }
}

auto PanelPlacement::Place(const Editor& editor) -> void
{
    std::array<View*, kPanels.size()> panels{};
    for (const auto kind : kPanels)
    {
        panels.at(static_cast<size_t>(kind)) = g_container->ByName(PanelName(kind));
    }
    const auto id_of = [&panels](PanelKind kind) -> std::optional<OverlayId>
    {
        const auto* panel = panels.at(static_cast<size_t>(kind));
        return panel != nullptr ? std::optional(panel->Id()) : std::nullopt;
    };
    m_preview_id = id_of(PanelKind::Preview);
    for (const auto kind : kLayerPanels)
    {
        m_layer_ids.at(static_cast<size_t>(LayerOf(kind).value_or(0))) = id_of(kind);
    }
    m_palette_id = id_of(PanelKind::Palette);

    std::optional<PanelKind> grabbed;
    for (const auto& pointer : g_interaction->Pointers())
    {
        if (!pointer.Interaction().grabbed)
        {
            continue;
        }
        const auto grabbed_id = pointer.Interaction().grabbed->grabbed_id;
        for (const auto kind : kPanels)
        {
            const auto* panel = panels.at(static_cast<size_t>(kind));
            if (panel == nullptr || panel->Id() != grabbed_id)
            {
                continue;
            }
            if (kind == PanelKind::Ghost || (kind == PanelKind::Palette && m_palette_on_hand))
            {
                continue;
            }
            m_anchor = Eigen::Affine3f(panel->Transform().matrix() * LocalTransform(editor, kind).matrix().inverse());
            grabbed = kind;
        }
    }

    const Eigen::Matrix3f facing = m_anchor.linear() / m_anchor.linear().col(0).norm();
    for (const auto kind : kPanels)
    {
        auto* panel = panels.at(static_cast<size_t>(kind));
        if (panel == nullptr)
        {
            continue;
        }
        const Eigen::Vector2f size = editor.PanelSize(kind);
        panel->SetInteractionTransform(
            View::UiTransform({static_cast<uint32_t>(std::lround(size.x())), static_cast<uint32_t>(std::lround(size.y()))})
        );
        panel->SetWantVisible(Shown(editor, kind));
        if (kind == grabbed)
        {
            continue;
        }
        if (kind == PanelKind::Ghost)
        {
            const auto& pointer = g_interaction->Pointers().at(editor.CarryPointer());
            const Eigen::Matrix3f hand = pointer.Pose().linear();
            Eigen::Affine3f pose = Eigen::Affine3f::Identity();
            pose.translate(Eigen::Vector3f(pointer.Pose().translation() + hand * -Eigen::Vector3f::UnitZ() * kGhostReach));
            pose.linear() = facing * kGhostWidth;
            panel->SetTransform(pose);
        }
        else if (kind == PanelKind::Palette && m_palette_on_hand)
        {
            const size_t hand = g_config->watch_hand == Handedness::Left ? 0 : 1;
            Eigen::Affine3f pose = g_interaction->Pointers().at(hand).TrackedPose();
            pose.translate(Eigen::Vector3f(0.0f, 0.05f, -0.06f));
            const Eigen::Matrix3f wrist = pose.linear();
            const Eigen::Matrix3f tilt = Eigen::AngleAxisf(-std::numbers::pi_v<float> / 3.0f, Eigen::Vector3f::UnitX())
                                             .toRotationMatrix();
            pose.linear() = wrist * tilt;
            pose.scale(kHandPaletteWidth);
            panel->SetTransform(pose);
        }
        else
        {
            panel->SetTransform(m_anchor * LocalTransform(editor, kind));
        }
        panel->SetDirty(true);
    }
}

auto PanelPlacement::Lines(const Editor& editor) const -> std::vector<EditorLine>
{
    std::vector<EditorLine> lines;
    if (editor.Closing())
    {
        return lines;
    }
    const auto& watch = editor.WatchDesign();
    const auto& keyboard = editor.KeyboardDesign();
    const auto watch_selected = watch.Selected() && *watch.Selected() < watch.Document().Components().size();
    auto preview_kind = PanelKind::Preview;
    if (editor.Exploded() && watch_selected)
    {
        preview_kind = kLayerPanels.at(static_cast<size_t>(FormDesigner::LayerOf(watch.Document().Components()[*watch.Selected()])));
    }
    auto* preview = g_container->ByName(PanelName(preview_kind));
    auto* properties = g_container->ByName(PanelName(PanelKind::Properties));
    if (preview == nullptr || properties == nullptr)
    {
        return lines;
    }
    const auto preview_corners = Container::Corners(*preview);
    const auto property_corners = Container::Corners(*properties);
    if (!preview_corners || !property_corners)
    {
        return lines;
    }
    std::optional<Eigen::Vector2f> anchor;
    if (editor.Target() == EditTarget::Watch && watch_selected)
    {
        const auto rect = watch.VisualRect(*watch.Selected());
        anchor = watch.ToPreview({rect.x + rect.w, rect.y + rect.h / 2.0f});
    }
    else if (editor.Target() == EditTarget::Keyboard && keyboard.Selected() && *keyboard.Selected() < keyboard.Document().Keys().size())
    {
        anchor = keyboard.KeyPreviewCenter(*keyboard.Selected());
    }
    if (anchor)
    {
        const Eigen::Vector2f size = editor.PanelSize(PanelKind::Preview);
        const Eigen::Vector2f uv = anchor->cwiseQuotient(size).cwiseMax(0.0f).cwiseMin(1.0f);
        lines.push_back(LineBetween(PointOn(*preview_corners, uv), PointOn(*property_corners, {0.0f, 0.5f})));
    }
    if (editor.Dialog() != DialogKind::None)
    {
        if (auto* dialog = g_container->ByName(PanelName(PanelKind::Dialog)))
        {
            if (const auto dialog_corners = Container::Corners(*dialog))
            {
                lines.push_back(LineBetween(PointOn(*property_corners, {1.0f, 0.5f}), PointOn(*dialog_corners, {0.0f, 0.5f})));
            }
        }
    }
    return lines;
}
