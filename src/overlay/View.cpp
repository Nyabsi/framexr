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

#include "View.hpp"

#include <atomic>

#include "Config.hpp"

View::View(std::unique_ptr<Compose> compose) : m_compose(compose ? std::move(compose) : std::make_unique<Compose>()) {
}

auto View::Init() -> Result<> {
    if (!Curvature()) {
        for (const auto& [name, value] : g_config->curve_values) {
            if (name == Name()) {
                SetCurvature(value);
            }
        }
    }

    if (AttachedTo() == RelativeTo::None) {
        bool hard_reset = true;
        for (const auto& [name, transform] : g_config->transform_values) {
            if (name == Name()) {
                SetSavedTransform(transform);
                hard_reset = false;
            }
        }
        Reset(hard_reset);
    }
    return {};
}

auto View::Update() -> void {
}

auto View::OnHover(const PointerHit& hit) -> std::optional<Haptics> {
    return m_compose->OnHover(hit);
}

auto View::OnLeft(size_t pointer) -> void {
    m_compose->OnLeft(pointer);
}

auto View::OnPointer(const PointerHit& hit, bool pressed) -> void {
    m_compose->OnPointer(hit, pressed);
}

auto View::OnScroll(const PointerHit& hit, float delta_y, float delta_x) -> void {
    m_compose->OnScroll(hit, delta_y, delta_x);
}

auto View::Accepts(const Eigen::Vector2f&) const -> bool {
    return true;
}

auto View::NextId() -> OverlayId {
    static std::atomic<uint64_t> next{0};
    return static_cast<OverlayId>(next.fetch_add(1, std::memory_order_relaxed));
}

auto View::UiTransform(VkExtent2D extent) -> Eigen::Affine2f {
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const Eigen::Vector2f scale = aspect < 1.0f ? Eigen::Vector2f(1.0f / aspect, -1.0f) : Eigen::Vector2f(1.0f, -aspect);
    Eigen::Affine2f transform = Eigen::Affine2f::Identity();
    transform.linear() = scale.asDiagonal();
    transform.translation() = Eigen::Vector2f(0.5f, 0.5f);
    return transform;
}

auto View::ParentTransform() const -> std::optional<Eigen::Affine3f> {
    switch (m_attached_to) {
    case RelativeTo::Head: return g_interaction->Hmd();
    case RelativeTo::LeftHand: return g_interaction->Pointers()[0].TrackedPose();
    case RelativeTo::RightHand: return g_interaction->Pointers()[1].TrackedPose();
    case RelativeTo::None:
    case RelativeTo::Stage: return std::nullopt;
    }
    return std::nullopt;
}

auto View::LocalTransform() const -> Eigen::Affine3f {
    if (m_saved_transform) {
        return *m_saved_transform;
    }
    Eigen::Affine3f local = Eigen::Affine3f::Identity();
    local.linear() = m_spawn_rotation.toRotationMatrix() * m_spawn_scale;
    local.translation() = m_spawn_point;
    return local;
}

auto View::AutoMovement() -> void {
    if (const auto parent = ParentTransform()) {
        m_transform = *parent * LocalTransform();
        m_dirty = true;
    }
}

auto View::Reset(bool hard_reset) -> void {
    if (hard_reset) {
        m_saved_transform.reset();
    }
    m_transform = ParentTransform().value_or(g_interaction->Hmd()) * LocalTransform();
    m_dirty = true;
}

auto View::RememberRelativeToHead() -> Eigen::Affine3f {
    const Eigen::Affine3f relative(g_interaction->Hmd().matrix().inverse() * m_transform.matrix());
    m_saved_transform = relative;
    return relative;
}
