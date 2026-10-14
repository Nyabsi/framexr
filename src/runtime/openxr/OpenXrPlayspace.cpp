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

#include "OpenXrPlayspace.hpp"

#include <algorithm>
#include <cmath>

#include "Config.hpp"
#include "Interaction.hpp"
#include "Logger.hpp"
#include "Monado.hpp"
#include "Container.hpp"

auto OpenXrPlayspace::Create(Monado& monado) -> Result<OpenXrPlayspace>
{
    LOG_INFO("Monado: using space offset API");

    const auto stage = monado.ReferenceSpaceOffset(MonadoReferenceSpace::Stage);
    if (!stage)
    {
        return Unexpected("Space offsets not supported.");
    }

    const auto& position = stage->position;
    const auto& orientation = stage->orientation;
    LOG_DEBUG(
        "STAGE is at [{}, {}, {}], [{}, {}, {}, {}]",
        position.x(),
        position.y(),
        position.z(),
        orientation.x(),
        orientation.y(),
        orientation.z(),
        orientation.w()
    );

    Eigen::Affine3f last_transform = Eigen::Affine3f::Identity();
    last_transform.linear() = orientation.toRotationMatrix();
    last_transform.translation() = position;
    return OpenXrPlayspace(last_transform);
}

OpenXrPlayspace::OpenXrPlayspace(const Eigen::Affine3f& last_transform) : m_last_transform(last_transform)
{
}

auto OpenXrPlayspace::Update(Monado& monado) -> void
{
    for (const auto& pointer : g_interaction->Pointers())
    {
        if (pointer.Now().space_reset)
        {
            if (!pointer.Before().space_reset)
            {
                LOG_INFO("Space reset");
                ResetOffset(monado);
            }
            return;
        }
    }

    if (UpdateRotate(monado))
    {
        return;
    }
    UpdateDrag(monado);
}

auto OpenXrPlayspace::UpdateRotate(Monado& monado) -> bool
{
    if (m_rotate)
    {
        const auto& pointer = g_interaction->Pointers().at(m_rotate->hand);
        if (!pointer.Now().space_rotate)
        {
            m_last_transform = m_rotate->pose;
            m_rotate.reset();
            LOG_INFO("End space rotate");
            return true;
        }

        const Eigen::Quaternionf new_hand(Eigen::Matrix3f((m_rotate->pose * pointer.RawPose()).linear()));
        const Eigen::Quaternionf dq = new_hand * m_rotate->hand_rotation.conjugate();

        Eigen::Affine3f space_transform = Eigen::Affine3f::Identity();
        if (g_config->space_rotate_unlocked)
        {
            space_transform.linear() = dq.toRotationMatrix();
        }
        else
        {
            const float rel_y = std::atan2(
                2.0f * std::fma(dq.y(), dq.w(), dq.x() * dq.z()),
                std::fma(2.0f, std::fma(dq.w(), dq.w(), dq.x() * dq.x()), -1.0f)
            );
            space_transform.linear() = Eigen::AngleAxisf(rel_y, Eigen::Vector3f::UnitY()).toRotationMatrix();
        }
        const Eigen::Vector3f hmd = g_interaction->Hmd().translation();
        const Eigen::Matrix3f space_rotation = space_transform.linear();
        space_transform.translation() = (space_rotation * hmd - hmd) * -1.0f;

        m_rotate->pose = m_rotate->pose * space_transform;
        m_rotate->hand_rotation = new_hand;
        ApplyOffset(m_rotate->pose, monado);
        return false;
    }

    for (size_t i = 0; i < g_interaction->Pointers().size(); ++i)
    {
        const auto& pointer = g_interaction->Pointers().at(i);
        if (!pointer.Now().space_rotate)
        {
            continue;
        }
        const Eigen::Quaternionf hand_rotation(Eigen::Matrix3f((m_last_transform * pointer.RawPose()).linear()));
        m_rotate = OpenXrPlayspaceRotate{.pose = m_last_transform, .hand = i, .hand_rotation = hand_rotation};
        m_drag.reset();
        LOG_INFO("Start space rotate");
        return true;
    }
    return false;
}

auto OpenXrPlayspace::UpdateDrag(Monado& monado) -> void
{
    if (m_drag)
    {
        const auto& pointer = g_interaction->Pointers().at(m_drag->hand);
        if (!pointer.Now().space_drag)
        {
            m_last_transform = m_drag->pose;
            m_drag.reset();
            LOG_INFO("End space drag");
            return;
        }

        const Eigen::Vector3f new_hand = m_drag->pose * pointer.RawPose().translation();
        const Eigen::Vector3f relative = (new_hand - m_drag->hand_position) * g_config->space_drag_multiplier;
        if (relative.squaredNorm() > 1000.0f)
        {
            LOG_WARN("Space drag too fast, ignoring");
            return;
        }

        const Eigen::Matrix3f inverse_rotation = Eigen::Affine3f(m_drag->pose.matrix().inverse()).linear();
        const Eigen::Vector3f overlay_offset = inverse_rotation * relative * -1.0f;
        for (auto& overlay : g_container->Views())
        {
            if (overlay->Grabbable())
            {
                overlay->SetDirty(true);
                overlay->Transform().translation() += overlay_offset;
            }
        }

        m_drag->pose.translation() += relative;
        m_drag->hand_position = new_hand;
        ApplyOffset(m_drag->pose, monado);
        return;
    }

    for (size_t i = 0; i < g_interaction->Pointers().size(); ++i)
    {
        const auto& pointer = g_interaction->Pointers().at(i);
        if (!pointer.Now().space_drag)
        {
            continue;
        }
        m_drag = OpenXrPlayspaceDrag{
            .pose = m_last_transform,
            .hand = i,
            .hand_position = m_last_transform * pointer.RawPose().translation()
        };
        LOG_INFO("Start space drag");
        return;
    }
}

auto OpenXrPlayspace::InterruptMoves(std::string_view reason) -> void
{
    if (m_drag)
    {
        LOG_INFO("Space drag interrupted by {}", reason);
        m_drag.reset();
    }
    if (m_rotate)
    {
        LOG_INFO("Space rotate interrupted by {}", reason);
        m_rotate.reset();
    }
}

auto OpenXrPlayspace::ResetOffset(Monado& monado) -> void
{
    InterruptMoves("manual reset");
    m_last_transform = Eigen::Affine3f::Identity();
    ApplyOffset(m_last_transform, monado);
}

auto OpenXrPlayspace::FixFloor(Monado& monado) -> void
{
    InterruptMoves("fix floor");
    const float y1 = g_interaction->Pointers()[0].RawPose().translation().y();
    const float y2 = g_interaction->Pointers()[1].RawPose().translation().y();
    m_last_transform.translation().y() += std::min(y1, y2) - 0.03f;
    ApplyOffset(m_last_transform, monado);
}

auto OpenXrPlayspace::ApplyOffset(const Eigen::Affine3f& transform, Monado& monado) -> void
{
    const MonadoPose pose{
        .orientation = Eigen::Quaternionf(Eigen::Matrix3f(transform.linear())),
        .position = transform.translation(),
    };
    if (const auto result = monado.SetReferenceSpaceOffset(MonadoReferenceSpace::Stage, pose); !result)
    {
        LOG_DEBUG("Failed to set STAGE offset: {}", Monado::ResultName(result.error()));
    }
}
