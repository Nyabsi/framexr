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

#include "OpenVrPlayspace.hpp"

#include <cmath>
#include <vector>

#include "Config.hpp"
#include "Interaction.hpp"
#include "Logger.hpp"
#include "OpenVrMath.hpp"
#include "Container.hpp"

auto OpenVrPlayspace::UniverseName(vr::ETrackingUniverseOrigin universe) -> std::string_view {
    switch (universe) {
    case vr::TrackingUniverseSeated: return "Seated";
    case vr::TrackingUniverseStanding: return "Standing";
    case vr::TrackingUniverseRawAndUncalibrated: return "Raw";
    }
    return "Raw";
}

auto OpenVrPlayspace::WorkingCopy() const -> std::optional<Eigen::Affine3f> {
    auto* chaperone = vr::VRChaperoneSetup();
    chaperone->RevertWorkingCopy();
    vr::HmdMatrix34_t matrix{};
    const bool valid = m_universe == vr::TrackingUniverseStanding ? chaperone->GetWorkingStandingZeroPoseToRawTrackingPose(&matrix)
                                                                  : chaperone->GetWorkingSeatedZeroPoseToRawTrackingPose(&matrix);
    if (!valid) {
        return std::nullopt;
    }
    return OpenVrMath::ToAffine(matrix);
}

auto OpenVrPlayspace::SetWorkingCopy(const Eigen::Affine3f& pose) const -> void {
    const auto matrix = OpenVrMath::ToHmdMatrix(pose);
    if (m_universe == vr::TrackingUniverseStanding) {
        vr::VRChaperoneSetup()->SetWorkingStandingZeroPoseToRawTrackingPose(&matrix);
    } else {
        vr::VRChaperoneSetup()->SetWorkingSeatedZeroPoseToRawTrackingPose(&matrix);
    }
}

auto OpenVrPlayspace::ApplyChaperoneOffset(const Eigen::Vector3f& offset) -> void {
    auto* chaperone = vr::VRChaperoneSetup();
    uint32_t count = 0;
    chaperone->GetLiveCollisionBoundsInfo(nullptr, &count);
    std::vector<vr::HmdQuad_t> quads(count);
    chaperone->GetLiveCollisionBoundsInfo(quads.data(), &count);
    for (auto& quad : quads) {
        for (auto& corner : quad.vCorners) {
            corner.v[0] += offset.x();
            corner.v[2] += offset.z();
        }
    }
    chaperone->SetWorkingCollisionBoundsInfo(quads.data(), count);
}

auto OpenVrPlayspace::ApplyChaperoneTransform(const Eigen::Affine3f& transform) -> void {
    auto* chaperone = vr::VRChaperoneSetup();
    uint32_t count = 0;
    chaperone->GetLiveCollisionBoundsInfo(nullptr, &count);
    std::vector<vr::HmdQuad_t> quads(count);
    chaperone->GetLiveCollisionBoundsInfo(quads.data(), &count);
    for (auto& quad : quads) {
        for (auto& corner : quad.vCorners) {
            const Eigen::Vector3f coordinate = transform * Eigen::Vector3f(corner.v[0], corner.v[1], corner.v[2]);
            corner.v[0] = coordinate.x();
            corner.v[2] = coordinate.z();
        }
    }
    chaperone->SetWorkingCollisionBoundsInfo(quads.data(), count);
}

auto OpenVrPlayspace::UpdateRotate() -> bool {
    if (m_rotate) {
        const auto& pointer = g_interaction->Pointers()[m_rotate->hand];
        if (!pointer.Now().space_rotate) {
            m_rotate.reset();
            LOG_INFO("End space rotate");
            return true;
        }

        const Eigen::Matrix3f new_hand_rotation = (m_rotate->pose * pointer.RawPose()).linear();
        const Eigen::Quaternionf new_hand(new_hand_rotation);
        const Eigen::Quaternionf dq = new_hand * m_rotate->hand_rotation.conjugate();
        const float rel_y = std::atan2(
            2.0f * std::fma(dq.y(), dq.w(), dq.x() * dq.z()),
            std::fma(2.0f, std::fma(dq.w(), dq.w(), dq.x() * dq.x()), -1.0f)
        );

        const Eigen::Vector3f hmd = g_interaction->Hmd().translation();
        Eigen::Affine3f space_transform(Eigen::AngleAxisf(rel_y, Eigen::Vector3f::UnitY()));
        const Eigen::Matrix3f space_rotation = space_transform.linear();
        const Eigen::Vector3f offset = (space_rotation * hmd - hmd) * -1.0f;
        Eigen::Affine3f overlay_transform(Eigen::AngleAxisf(-rel_y, Eigen::Vector3f::UnitY()));
        overlay_transform.translation() = offset;
        space_transform.translation() = offset;

        for (auto& overlay : g_container->Views()) {
            if (overlay->Grabbable()) {
                overlay->SetDirty(true);
                overlay->Transform().translation() = overlay_transform * overlay->Transform().translation();
            }
        }

        m_rotate->pose = m_rotate->pose * space_transform;
        m_rotate->hand_rotation = new_hand;

        if (m_universe == vr::TrackingUniverseStanding) {
            ApplyChaperoneTransform(Eigen::Affine3f(space_transform.matrix().inverse()));
        }
        SetWorkingCopy(m_rotate->pose);
        vr::VRChaperoneSetup()->CommitWorkingCopy(vr::EChaperoneConfigFile_Live);
        return false;
    }

    for (size_t i = 0; i < g_interaction->Pointers().size(); ++i) {
        const auto& pointer = g_interaction->Pointers()[i];
        if (!pointer.Now().space_rotate) {
            continue;
        }
        const auto pose = WorkingCopy();
        if (!pose) {
            LOG_WARN("Can't space rotate - failed to get zero pose");
            return true;
        }
        const Eigen::Matrix3f hand_rotation = (*pose * pointer.RawPose()).linear();
        m_rotate = OpenVrPlayspaceRotate{.pose = *pose, .hand = i, .hand_rotation = Eigen::Quaternionf(hand_rotation)};
        m_drag.reset();
        LOG_INFO("Start space rotate");
        return true;
    }
    return false;
}

auto OpenVrPlayspace::UpdateDrag() -> void {
    if (m_drag) {
        const auto& pointer = g_interaction->Pointers()[m_drag->hand];
        if (!pointer.Now().space_drag) {
            m_drag.reset();
            LOG_INFO("End space drag");
            return;
        }

        const Eigen::Vector3f new_hand = m_drag->pose * pointer.RawPose().translation();
        const Eigen::Vector3f relative = (new_hand - m_drag->hand_position) * g_config->space_drag_multiplier;
        if (relative.squaredNorm() > 1000.0f) {
            LOG_WARN("Space drag too fast, ignoring");
            return;
        }

        const Eigen::Matrix3f inverse_rotation = m_drag->pose.linear().inverse();
        const Eigen::Vector3f overlay_offset = inverse_rotation * relative * -1.0f;
        for (auto& overlay : g_container->Views()) {
            if (overlay->Grabbable()) {
                overlay->SetDirty(true);
                overlay->Transform().translation() += overlay_offset;
            }
        }

        m_drag->pose.translation() += relative;
        m_drag->hand_position = new_hand;

        if (m_universe == vr::TrackingUniverseStanding) {
            ApplyChaperoneOffset(overlay_offset);
        }
        SetWorkingCopy(m_drag->pose);
        vr::VRChaperoneSetup()->CommitWorkingCopy(vr::EChaperoneConfigFile_Live);
        return;
    }

    for (size_t i = 0; i < g_interaction->Pointers().size(); ++i) {
        const auto& pointer = g_interaction->Pointers()[i];
        if (!pointer.Now().space_drag) {
            continue;
        }
        const auto pose = WorkingCopy();
        if (!pose) {
            LOG_WARN("Can't space drag - failed to get zero pose");
            return;
        }
        m_drag = OpenVrPlayspaceDrag{.pose = *pose, .hand = i, .hand_position = *pose * pointer.RawPose().translation()};
        m_rotate.reset();
        LOG_INFO("Start space drag");
        return;
    }
}

auto OpenVrPlayspace::Update() -> void {
    if (UpdateRotate()) {
        return;
    }
    UpdateDrag();
}

auto OpenVrPlayspace::InterruptMoves(std::string_view reason) -> void {
    if (m_drag) {
        LOG_INFO("Space drag interrupted by {}", reason);
        m_drag.reset();
    }
    if (m_rotate) {
        LOG_INFO("Space rotate interrupted by {}", reason);
        m_rotate.reset();
    }
}

auto OpenVrPlayspace::ResetOffset() -> void {
    float height = 1.6f;
    if (const auto pose = WorkingCopy()) {
        height = g_interaction->Hmd().translation().y() - pose->translation().y();
        if (m_universe == vr::TrackingUniverseStanding) {
            ApplyChaperoneTransform(*pose);
        }
    }

    Eigen::Affine3f transform = Eigen::Affine3f::Identity();
    if (m_universe == vr::TrackingUniverseSeated) {
        transform.translation() = -Eigen::Vector3f::UnitY() * height;
    }
    SetWorkingCopy(transform);
    vr::VRChaperoneSetup()->CommitWorkingCopy(vr::EChaperoneConfigFile_Live);
    InterruptMoves("manual reset");
}

auto OpenVrPlayspace::FixFloor() -> void {
    const float y1 = g_interaction->Pointers()[0].TrackedPose().translation().y();
    const float y2 = g_interaction->Pointers()[1].TrackedPose().translation().y();
    auto pose = WorkingCopy();
    if (!pose) {
        LOG_WARN("Can't fix floor - failed to get zero pose");
        return;
    }
    pose->translation().y() += std::min(y1, y2) - 0.03f;
    SetWorkingCopy(*pose);
    vr::VRChaperoneSetup()->CommitWorkingCopy(vr::EChaperoneConfigFile_Live);
    InterruptMoves("fix floor");
}

auto OpenVrPlayspace::PlayspaceChanged() -> void {
    const auto universe = vr::VRCompositor()->GetTrackingSpace();
    if (universe != m_universe) {
        LOG_INFO("Playspace changed: {} -> {}", UniverseName(m_universe), UniverseName(universe));
        m_universe = universe;
    }
    InterruptMoves("external change");
}
