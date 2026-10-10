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

#include "Logger.hpp"
#include "Monado.hpp"
#include "OverlayContainer.hpp"
#include "State.hpp"

auto OpenXrPlayspace::Create(Monado& monado) -> Result<OpenXrPlayspace> {
    LOG_INFO("Monado: using space offset API");

    const auto stage = monado.ReferenceSpaceOffset(MonadoReferenceSpace::Stage);
    if (!stage) {
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

OpenXrPlayspace::OpenXrPlayspace(const Eigen::Affine3f& last_transform) : last_transform_(last_transform) {
}

auto OpenXrPlayspace::Update(OverlayContainer& overlays, const AppState& app, Monado& monado) -> void {
    for (const auto& pointer : app.input.pointers) {
        if (pointer.now.space_reset) {
            if (!pointer.before.space_reset) {
                LOG_INFO("Space reset");
                ResetOffset(monado);
            }
            return;
        }
    }

    if (UpdateRotate(app, monado)) {
        return;
    }
    UpdateDrag(overlays, app, monado);
}

auto OpenXrPlayspace::UpdateRotate(const AppState& app, Monado& monado) -> bool {
    if (rotate_) {
        const auto& pointer = app.input.pointers.at(rotate_->hand);
        if (!pointer.now.space_rotate) {
            last_transform_ = rotate_->pose;
            rotate_.reset();
            LOG_INFO("End space rotate");
            return true;
        }

        const Eigen::Quaternionf new_hand(Eigen::Matrix3f((rotate_->pose * pointer.raw_pose).linear()));
        const Eigen::Quaternionf dq = new_hand * rotate_->hand_rotation.conjugate();

        Eigen::Affine3f space_transform = Eigen::Affine3f::Identity();
        if (app.config.space_rotate_unlocked) {
            space_transform.linear() = dq.toRotationMatrix();
        } else {
            const float rel_y = std::atan2(
                2.0f * std::fma(dq.y(), dq.w(), dq.x() * dq.z()),
                std::fma(2.0f, std::fma(dq.w(), dq.w(), dq.x() * dq.x()), -1.0f)
            );
            space_transform.linear() = Eigen::AngleAxisf(rel_y, Eigen::Vector3f::UnitY()).toRotationMatrix();
        }
        const Eigen::Vector3f hmd = app.input.hmd.translation();
        const Eigen::Matrix3f space_rotation = space_transform.linear();
        space_transform.translation() = (space_rotation * hmd - hmd) * -1.0f;

        rotate_->pose = rotate_->pose * space_transform;
        rotate_->hand_rotation = new_hand;
        ApplyOffset(rotate_->pose, monado);
        return false;
    }

    for (size_t i = 0; i < app.input.pointers.size(); ++i) {
        const auto& pointer = app.input.pointers.at(i);
        if (!pointer.now.space_rotate) {
            continue;
        }
        const Eigen::Quaternionf hand_rotation(Eigen::Matrix3f((last_transform_ * pointer.raw_pose).linear()));
        rotate_ = OpenXrPlayspaceRotate{.pose = last_transform_, .hand = i, .hand_rotation = hand_rotation};
        drag_.reset();
        LOG_INFO("Start space rotate");
        return true;
    }
    return false;
}

auto OpenXrPlayspace::UpdateDrag(OverlayContainer& overlays, const AppState& app, Monado& monado) -> void {
    if (drag_) {
        const auto& pointer = app.input.pointers.at(drag_->hand);
        if (!pointer.now.space_drag) {
            last_transform_ = drag_->pose;
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

        const Eigen::Matrix3f inverse_rotation = Eigen::Affine3f(drag_->pose.matrix().inverse()).linear();
        const Eigen::Vector3f overlay_offset = inverse_rotation * relative * -1.0f;
        for (auto& overlay : overlays.Overlays()) {
            if (overlay->state.grabbable) {
                overlay->state.dirty = true;
                overlay->state.transform.translation() += overlay_offset;
            }
        }

        drag_->pose.translation() += relative;
        drag_->hand_position = new_hand;
        ApplyOffset(drag_->pose, monado);
        return;
    }

    for (size_t i = 0; i < app.input.pointers.size(); ++i) {
        const auto& pointer = app.input.pointers.at(i);
        if (!pointer.now.space_drag) {
            continue;
        }
        drag_ = OpenXrPlayspaceDrag{
            .pose = last_transform_,
            .hand = i,
            .hand_position = last_transform_ * pointer.raw_pose.translation()
        };
        LOG_INFO("Start space drag");
        return;
    }
}

auto OpenXrPlayspace::InterruptMoves(std::string_view reason) -> void {
    if (drag_) {
        LOG_INFO("Space drag interrupted by {}", reason);
        drag_.reset();
    }
    if (rotate_) {
        LOG_INFO("Space rotate interrupted by {}", reason);
        rotate_.reset();
    }
}

auto OpenXrPlayspace::ResetOffset(Monado& monado) -> void {
    InterruptMoves("manual reset");
    last_transform_ = Eigen::Affine3f::Identity();
    ApplyOffset(last_transform_, monado);
}

auto OpenXrPlayspace::FixFloor(const InputState& input, Monado& monado) -> void {
    InterruptMoves("fix floor");
    const float y1 = input.pointers[0].raw_pose.translation().y();
    const float y2 = input.pointers[1].raw_pose.translation().y();
    last_transform_.translation().y() += std::min(y1, y2) - 0.03f;
    ApplyOffset(last_transform_, monado);
}

auto OpenXrPlayspace::ApplyOffset(const Eigen::Affine3f& transform, Monado& monado) -> void {
    const MonadoPose pose{
        .orientation = Eigen::Quaternionf(Eigen::Matrix3f(transform.linear())),
        .position = transform.translation(),
    };
    if (const auto result = monado.SetReferenceSpaceOffset(MonadoReferenceSpace::Stage, pose); !result) {
        LOG_DEBUG("Failed to set STAGE offset: {}", Monado::ResultName(result.error()));
    }
}
