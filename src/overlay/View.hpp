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

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <Eigen/Geometry>
#include <vulkan/vulkan.h>

#include "Compose.hpp"
#include "Error.hpp"
#include "Interaction.hpp"
#include "methods/LaserPointer.hpp"

constexpr uint32_t kZOrderToast = 70;

constexpr uint32_t kZOrderLines = 69;

constexpr uint32_t kZOrderWatch = 68;

constexpr uint32_t kZOrderDefault = 0;

enum class RelativeTo : uint8_t
{
    None,
    Head,
    LeftHand,
    RightHand,
    Stage,
};

class View
{
  public:
    explicit View(std::unique_ptr<Compose> compose = nullptr);
    virtual ~View() = default;
    View(const View&) = delete;
    auto operator=(const View&) -> View& = delete;

    static auto NextId() -> OverlayId;
    static auto UiTransform(VkExtent2D extent) -> Eigen::Affine2f;

    [[nodiscard]] auto Id() const -> OverlayId { return m_id; }
    [[nodiscard]] auto Name() const -> const std::string& { return m_name; }
    [[nodiscard]] auto Name() -> std::string& { return m_name; }
    [[nodiscard]] auto WantVisible() const -> bool { return m_want_visible; }
    [[nodiscard]] auto ShowHide() const -> bool { return m_show_hide; }
    [[nodiscard]] auto Grabbable() const -> bool { return m_grabbable; }
    [[nodiscard]] auto Interactable() const -> bool { return m_interactable; }
    [[nodiscard]] auto Recenter() const -> bool { return m_recenter; }
    [[nodiscard]] auto Dirty() const -> bool { return m_dirty; }
    [[nodiscard]] auto Alpha() const -> float { return m_alpha; }
    [[nodiscard]] auto ZOrder() const -> uint32_t { return m_z_order; }
    [[nodiscard]] auto Transform() const -> const Eigen::Affine3f& { return m_transform; }
    [[nodiscard]] auto Transform() -> Eigen::Affine3f& { return m_transform; }
    [[nodiscard]] auto SpawnScale() const -> float { return m_spawn_scale; }
    [[nodiscard]] auto SpawnPoint() const -> const Eigen::Vector3f& { return m_spawn_point; }
    [[nodiscard]] auto SpawnPoint() -> Eigen::Vector3f& { return m_spawn_point; }
    [[nodiscard]] auto SpawnRotation() const -> const Eigen::Quaternionf& { return m_spawn_rotation; }
    [[nodiscard]] auto SpawnRotation() -> Eigen::Quaternionf& { return m_spawn_rotation; }
    [[nodiscard]] auto SavedTransform() const -> const std::optional<Eigen::Affine3f>& { return m_saved_transform; }
    [[nodiscard]] auto SavedTransform() -> std::optional<Eigen::Affine3f>& { return m_saved_transform; }
    [[nodiscard]] auto AttachedTo() const -> RelativeTo { return m_attached_to; }
    [[nodiscard]] auto Curvature() const -> const std::optional<float>& { return m_curvature; }
    [[nodiscard]] auto Curvature() -> std::optional<float>& { return m_curvature; }
    [[nodiscard]] auto InteractionTransform() const -> const Eigen::Affine2f& { return m_interaction_transform; }
    [[nodiscard]] auto InteractionTransform() -> Eigen::Affine2f& { return m_interaction_transform; }
    [[nodiscard]] auto ExtentHint() const -> VkExtent2D { return m_extent_hint; }
    [[nodiscard]] auto BirthFrame() const -> size_t { return m_birth_frame; }

    auto SetName(std::string name) -> void { m_name = std::move(name); }
    auto SetWantVisible(bool want_visible) -> void { m_want_visible = want_visible; }
    auto SetShowHide(bool show_hide) -> void { m_show_hide = show_hide; }
    auto SetGrabbable(bool grabbable) -> void { m_grabbable = grabbable; }
    auto SetInteractable(bool interactable) -> void { m_interactable = interactable; }
    auto SetRecenter(bool recenter) -> void { m_recenter = recenter; }
    auto SetDirty(bool dirty) -> void { m_dirty = dirty; }
    auto SetAlpha(float alpha) -> void { m_alpha = alpha; }
    auto SetZOrder(uint32_t z_order) -> void { m_z_order = z_order; }
    auto SetTransform(Eigen::Affine3f transform) -> void { m_transform = std::move(transform); }
    auto SetSpawnScale(float spawn_scale) -> void { m_spawn_scale = spawn_scale; }
    auto SetSpawnPoint(Eigen::Vector3f spawn_point) -> void { m_spawn_point = std::move(spawn_point); }
    auto SetSpawnRotation(Eigen::Quaternionf spawn_rotation) -> void { m_spawn_rotation = std::move(spawn_rotation); }
    auto SetSavedTransform(std::optional<Eigen::Affine3f> saved_transform) -> void
    {
        m_saved_transform = std::move(saved_transform);
    }
    auto SetAttachedTo(RelativeTo attached_to) -> void { m_attached_to = attached_to; }
    auto SetCurvature(std::optional<float> curvature) -> void { m_curvature = curvature; }
    auto SetInteractionTransform(Eigen::Affine2f interaction_transform) -> void
    {
        m_interaction_transform = std::move(interaction_transform);
    }
    auto SetExtentHint(VkExtent2D extent_hint) -> void { m_extent_hint = extent_hint; }
    auto SetBirthFrame(size_t birth_frame) -> void { m_birth_frame = birth_frame; }

    [[nodiscard]] auto ParentTransform() const -> std::optional<Eigen::Affine3f>;
    [[nodiscard]] auto LocalTransform() const -> Eigen::Affine3f;

    [[nodiscard]] auto Composer() const -> Compose* { return m_compose.get(); }
    [[nodiscard]] auto TakeComposer() -> std::unique_ptr<Compose> { return std::move(m_compose); }
    [[nodiscard]] auto PrimaryPointer() const -> const std::optional<size_t>& { return m_primary_pointer; }
    auto SetComposer(std::unique_ptr<Compose> compose) -> void { m_compose = std::move(compose); }
    auto SetPrimaryPointer(std::optional<size_t> primary_pointer) -> void { m_primary_pointer = primary_pointer; }

    auto Init() -> Result<>;
    virtual auto Update() -> void;
    virtual auto OnHover(const PointerHit& hit) -> std::optional<Haptics>;
    virtual auto OnLeft(size_t pointer) -> void;
    virtual auto OnPointer(const PointerHit& hit, bool pressed) -> void;
    virtual auto OnScroll(const PointerHit& hit, float delta_y, float delta_x) -> void;
    [[nodiscard]] virtual auto Accepts(const Eigen::Vector2f& uv) const -> bool;
    auto AutoMovement() -> void;
    auto Reset(bool hard_reset) -> void;
    auto RememberRelativeToHead() -> Eigen::Affine3f;

  private:
    std::unique_ptr<Compose> m_compose;
    std::optional<size_t> m_primary_pointer;
    OverlayId m_id = NextId();
    std::string m_name;
    bool m_want_visible = false;
    bool m_show_hide = false;
    bool m_grabbable = false;
    bool m_interactable = false;
    bool m_recenter = false;
    bool m_dirty = true;
    float m_alpha = 1.0f;
    uint32_t m_z_order = kZOrderDefault;
    Eigen::Affine3f m_transform = Eigen::Affine3f::Identity();
    float m_spawn_scale = 1.0f;
    Eigen::Vector3f m_spawn_point = -Eigen::Vector3f::UnitZ();
    Eigen::Quaternionf m_spawn_rotation = Eigen::Quaternionf::Identity();
    std::optional<Eigen::Affine3f> m_saved_transform;
    RelativeTo m_attached_to = RelativeTo::None;
    std::optional<float> m_curvature;
    Eigen::Affine2f m_interaction_transform = Eigen::Affine2f::Identity();
    VkExtent2D m_extent_hint{};
    size_t m_birth_frame = 0;
};
