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

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <Eigen/Geometry>
#include <vulkan/vulkan.h>

#include "CommandBuffer.hpp"
#include "Error.hpp"
#include "Image.hpp"
#include "Input.hpp"

class AppState;

constexpr uint32_t kZOrderToast = 70;
constexpr uint32_t kZOrderLines = 69;
constexpr uint32_t kZOrderWatch = 68;
constexpr uint32_t kZOrderAnchor = 67;
constexpr uint32_t kZOrderDefault = 0;

enum class RelativeTo : uint8_t {
    None,
    Head,
    LeftHand,
    RightHand,
    Stage,
};

enum class RenderReadiness : uint8_t {
    Should,
    Can,
    Unable,
};

struct FrameMeta {
    VkExtent2D extent{};
    Eigen::Affine3f transform = Eigen::Affine3f::Identity();
    VkFormat format = VK_FORMAT_UNDEFINED;
};

class OverlayRenderer {
  public:
    virtual ~OverlayRenderer() = default;

    virtual auto Init(AppState& app) -> Result<> = 0;
    virtual auto Pause(AppState& app) -> Result<> = 0;
    virtual auto Resume(AppState& app) -> Result<> = 0;
    virtual auto ShouldRender(AppState& app) -> Result<RenderReadiness> = 0;
    virtual auto Render(AppState& app, const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> = 0;
    virtual auto Meta() -> std::optional<FrameMeta> = 0;
};

class InteractionHandler {
  public:
    virtual ~InteractionHandler() = default;

    virtual auto OnHover(AppState& app, const PointerHit& hit) -> std::optional<Haptics> = 0;
    virtual auto OnLeft(AppState& app, size_t pointer) -> void = 0;
    virtual auto OnPointer(AppState& app, const PointerHit& hit, bool pressed) -> void = 0;
    virtual auto OnScroll(AppState& app, const PointerHit& hit, float delta_y, float delta_x) -> void = 0;
};

class OverlayBackend : public OverlayRenderer, public InteractionHandler {
  public:
    virtual auto SetRenderer(std::unique_ptr<OverlayRenderer> renderer) -> void = 0;
    virtual auto SetInteraction(std::unique_ptr<InteractionHandler> interaction) -> void = 0;
};

class NullRenderer final : public OverlayRenderer {
  public:
    auto Init(AppState& app) -> Result<> override;
    auto Pause(AppState& app) -> Result<> override;
    auto Resume(AppState& app) -> Result<> override;
    auto ShouldRender(AppState& app) -> Result<RenderReadiness> override;
    auto Render(AppState& app, const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> override;
    auto Meta() -> std::optional<FrameMeta> override;
};

class NullInteraction final : public InteractionHandler {
  public:
    auto OnHover(AppState& app, const PointerHit& hit) -> std::optional<Haptics> override;
    auto OnLeft(AppState& app, size_t pointer) -> void override;
    auto OnPointer(AppState& app, const PointerHit& hit, bool pressed) -> void override;
    auto OnScroll(AppState& app, const PointerHit& hit, float delta_y, float delta_x) -> void override;
};

class SplitOverlayBackend final : public OverlayBackend {
  public:
    SplitOverlayBackend();
    SplitOverlayBackend(std::unique_ptr<OverlayRenderer> renderer, std::unique_ptr<InteractionHandler> interaction);

    auto SetRenderer(std::unique_ptr<OverlayRenderer> renderer) -> void override;
    auto SetInteraction(std::unique_ptr<InteractionHandler> interaction) -> void override;

    auto Init(AppState& app) -> Result<> override;
    auto Pause(AppState& app) -> Result<> override;
    auto Resume(AppState& app) -> Result<> override;
    auto ShouldRender(AppState& app) -> Result<RenderReadiness> override;
    auto Render(AppState& app, const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha) -> Result<bool> override;
    auto Meta() -> std::optional<FrameMeta> override;

    auto OnHover(AppState& app, const PointerHit& hit) -> std::optional<Haptics> override;
    auto OnLeft(AppState& app, size_t pointer) -> void override;
    auto OnPointer(AppState& app, const PointerHit& hit, bool pressed) -> void override;
    auto OnScroll(AppState& app, const PointerHit& hit, float delta_y, float delta_x) -> void override;

  private:
    std::unique_ptr<OverlayRenderer> renderer_;
    std::unique_ptr<InteractionHandler> interaction_;
};

struct OverlayState {
    OverlayId id = NextId();
    std::string name;
    bool want_visible = false;
    bool show_hide = false;
    bool grabbable = false;
    bool interactable = false;
    bool recenter = false;
    bool anchored = false;
    bool dirty = true;
    float alpha = 1.0f;
    uint32_t z_order = kZOrderDefault;
    Eigen::Affine3f transform = Eigen::Affine3f::Identity();
    float spawn_scale = 1.0f;
    Eigen::Vector3f spawn_point = -Eigen::Vector3f::UnitZ();
    Eigen::Quaternionf spawn_rotation = Eigen::Quaternionf::Identity();
    std::optional<Eigen::Affine3f> saved_transform;
    RelativeTo relative_to = RelativeTo::None;
    std::optional<float> curvature;
    Eigen::Affine2f interaction_transform = Eigen::Affine2f::Identity();
    size_t birth_frame = 0;

    static auto NextId() -> OverlayId;
    static auto UiTransform(VkExtent2D extent) -> Eigen::Affine2f;

    [[nodiscard]] auto ParentTransform(const AppState& app) const -> std::optional<Eigen::Affine3f>;
    [[nodiscard]] auto AnchorTransform(const AppState& app) const -> Eigen::Affine3f;
    [[nodiscard]] auto LocalTransform() const -> Eigen::Affine3f;

    auto AutoMovement(const AppState& app) -> void;
    auto Reset(const AppState& app, bool hard_reset) -> void;
    auto Realign(const Eigen::Affine3f& hmd) -> void;
};

class Overlay {
  public:
    Overlay(OverlayState state, std::unique_ptr<OverlayBackend> backend);

    OverlayState state;
    std::unique_ptr<OverlayBackend> backend;
    std::optional<size_t> primary_pointer;

    auto Init(AppState& app) -> Result<>;
};
