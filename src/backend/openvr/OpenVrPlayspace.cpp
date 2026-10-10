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

#include "Logger.hpp"
#include "OpenVrMath.hpp"
#include "OverlayContainer.hpp"
#include "State.hpp"

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
    const bool valid = universe_ == vr::TrackingUniverseStanding ? chaperone->GetWorkingStandingZeroPoseToRawTrackingPose(&matrix)
                                                                 : chaperone->GetWorkingSeatedZeroPoseToRawTrackingPose(&matrix);
    if (!valid) {
        return std::nullopt;
    }
    return OpenVrMath::ToAffine(matrix);
}

auto OpenVrPlayspace::SetWorkingCopy(const Eigen::Affine3f& pose) const -> void {
    const auto matrix = OpenVrMath::ToHmdMatrix(pose);
    if (universe_ == vr::TrackingUniverseStanding) {
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

auto OpenVrPlayspace::UpdateRotate(OverlayContainer& overlays, const AppState& app) -> bool {
    if (rotate_) {
        const auto& pointer = app.input.pointers[rotate_->hand];
        if (!pointer.now.space_rotate) {
            rotate_.reset();
            LOG_INFO("End space rotate");
            return true;
        }

        const Eigen::Matrix3f new_hand_rotation = (rotate_->pose * pointer.raw_pose).linear();
        const Eigen::Quaternionf new_hand(new_hand_rotation);
        const Eigen::Quaternionf dq = new_hand * rotate_->hand_rotation.conjugate();
        const float rel_y = std::atan2(
            2.0f * std::fma(dq.y(), dq.w(), dq.x() * dq.z()),
            std::fma(2.0f, std::fma(dq.w(), dq.w(), dq.x() * dq.x()), -1.0f)
        );

        const Eigen::Vector3f hmd = app.input.hmd.translation();
        Eigen::Affine3f space_transform(Eigen::AngleAxisf(rel_y, Eigen::Vector3f::UnitY()));
        const Eigen::Matrix3f space_rotation = space_transform.linear();
        const Eigen::Vector3f offset = (space_rotation * hmd - hmd) * -1.0f;
        Eigen::Affine3f overlay_transform(Eigen::AngleAxisf(-rel_y, Eigen::Vector3f::UnitY()));
        overlay_transform.translation() = offset;
        space_transform.translation() = offset;

        for (auto& overlay : overlays.Overlays()) {
            if (overlay->state.grabbable) {
                overlay->state.dirty = true;
                overlay->state.transform.translation() = overlay_transform * overlay->state.transform.translation();
            }
        }

        rotate_->pose = rotate_->pose * space_transform;
        rotate_->hand_rotation = new_hand;

        if (universe_ == vr::TrackingUniverseStanding) {
            ApplyChaperoneTransform(Eigen::Affine3f(space_transform.matrix().inverse()));
        }
        SetWorkingCopy(rotate_->pose);
        vr::VRChaperoneSetup()->CommitWorkingCopy(vr::EChaperoneConfigFile_Live);
        return false;
    }

    for (size_t i = 0; i < app.input.pointers.size(); ++i) {
        const auto& pointer = app.input.pointers[i];
        if (!pointer.now.space_rotate) {
            continue;
        }
        const auto pose = WorkingCopy();
        if (!pose) {
            LOG_WARN("Can't space rotate - failed to get zero pose");
            return true;
        }
        const Eigen::Matrix3f hand_rotation = (*pose * pointer.raw_pose).linear();
        rotate_ = OpenVrPlayspaceRotate{.pose = *pose, .hand = i, .hand_rotation = Eigen::Quaternionf(hand_rotation)};
        drag_.reset();
        LOG_INFO("Start space rotate");
        return true;
    }
    return false;
}

auto OpenVrPlayspace::UpdateDrag(OverlayContainer& overlays, const AppState& app) -> void {
    if (drag_) {
        const auto& pointer = app.input.pointers[drag_->hand];
        if (!pointer.now.space_drag) {
            drag_.reset();
            LOG_INFO("End space drag");
            return;
        }

        const Eigen::Vector3f new_hand = drag_->pose * pointer.raw_pose.translation();
        const Eigen::Vector3f relative = (new_hand - drag_->hand_position) * app.config.space_drag_multiplier;
        if (relative.squaredNorm() > 1000.0f) {
            LOG_WARN("Space drag too fast, ignoring");
            return;
        }

        const Eigen::Matrix3f inverse_rotation = drag_->pose.linear().inverse();
        const Eigen::Vector3f overlay_offset = inverse_rotation * relative * -1.0f;
        for (auto& overlay : overlays.Overlays()) {
            if (overlay->state.grabbable) {
                overlay->state.dirty = true;
                overlay->state.transform.translation() += overlay_offset;
            }
        }

        drag_->pose.translation() += relative;
        drag_->hand_position = new_hand;

        if (universe_ == vr::TrackingUniverseStanding) {
            ApplyChaperoneOffset(overlay_offset);
        }
        SetWorkingCopy(drag_->pose);
        vr::VRChaperoneSetup()->CommitWorkingCopy(vr::EChaperoneConfigFile_Live);
        return;
    }

    for (size_t i = 0; i < app.input.pointers.size(); ++i) {
        const auto& pointer = app.input.pointers[i];
        if (!pointer.now.space_drag) {
            continue;
        }
        const auto pose = WorkingCopy();
        if (!pose) {
            LOG_WARN("Can't space drag - failed to get zero pose");
            return;
        }
        drag_ = OpenVrPlayspaceDrag{.pose = *pose, .hand = i, .hand_position = *pose * pointer.raw_pose.translation()};
        rotate_.reset();
        LOG_INFO("Start space drag");
        return;
    }
}

auto OpenVrPlayspace::Update(OverlayContainer& overlays, const AppState& app) -> void {
    if (UpdateRotate(overlays, app)) {
        return;
    }
    UpdateDrag(overlays, app);
}

auto OpenVrPlayspace::InterruptMoves(std::string_view reason) -> void {
    if (drag_) {
        LOG_INFO("Space drag interrupted by {}", reason);
        drag_.reset();
    }
    if (rotate_) {
        LOG_INFO("Space rotate interrupted by {}", reason);
        rotate_.reset();
    }
}

auto OpenVrPlayspace::ResetOffset(const InputState& input) -> void {
    float height = 1.6f;
    if (const auto pose = WorkingCopy()) {
        height = input.hmd.translation().y() - pose->translation().y();
        if (universe_ == vr::TrackingUniverseStanding) {
            ApplyChaperoneTransform(*pose);
        }
    }

    Eigen::Affine3f transform = Eigen::Affine3f::Identity();
    if (universe_ == vr::TrackingUniverseSeated) {
        transform.translation() = -Eigen::Vector3f::UnitY() * height;
    }
    SetWorkingCopy(transform);
    vr::VRChaperoneSetup()->CommitWorkingCopy(vr::EChaperoneConfigFile_Live);
    InterruptMoves("manual reset");
}

auto OpenVrPlayspace::FixFloor(const InputState& input) -> void {
    const float y1 = input.pointers[0].pose.translation().y();
    const float y2 = input.pointers[1].pose.translation().y();
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
    if (universe != universe_) {
        LOG_INFO("Playspace changed: {} -> {}", UniverseName(universe_), UniverseName(universe));
        universe_ = universe;
    }
    InterruptMoves("external change");
}
