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

#include "Overlay.hpp"

#include <atomic>
#include <cmath>
#include <limits>
#include <numbers>

#include "State.hpp"

auto NullRenderer::Init(AppState&) -> Result<> {
    return {};
}

auto NullRenderer::Pause(AppState&) -> Result<> {
    return {};
}

auto NullRenderer::Resume(AppState&) -> Result<> {
    return {};
}

auto NullRenderer::ShouldRender(AppState&) -> Result<RenderReadiness> {
    return RenderReadiness::Unable;
}

auto NullRenderer::Render(AppState&, const std::shared_ptr<Image>&, CommandBatch&, float) -> Result<bool> {
    return false;
}

auto NullRenderer::Meta() -> std::optional<FrameMeta> {
    return std::nullopt;
}

auto NullInteraction::OnHover(AppState&, const PointerHit&) -> std::optional<Haptics> {
    return std::nullopt;
}

auto NullInteraction::OnLeft(AppState&, size_t) -> void {
}

auto NullInteraction::OnPointer(AppState&, const PointerHit&, bool) -> void {
}

auto NullInteraction::OnScroll(AppState&, const PointerHit&, float, float) -> void {
}

SplitOverlayBackend::SplitOverlayBackend()
    : renderer_(std::make_unique<NullRenderer>()),
      interaction_(std::make_unique<NullInteraction>()) {
}

SplitOverlayBackend::SplitOverlayBackend(std::unique_ptr<OverlayRenderer> renderer, std::unique_ptr<InteractionHandler> interaction)
    : renderer_(renderer ? std::move(renderer) : std::make_unique<NullRenderer>()),
      interaction_(interaction ? std::move(interaction) : std::make_unique<NullInteraction>()) {
}

auto SplitOverlayBackend::SetRenderer(std::unique_ptr<OverlayRenderer> renderer) -> void {
    renderer_ = std::move(renderer);
}

auto SplitOverlayBackend::SetInteraction(std::unique_ptr<InteractionHandler> interaction) -> void {
    interaction_ = std::move(interaction);
}

auto SplitOverlayBackend::Init(AppState& app) -> Result<> {
    return renderer_->Init(app);
}

auto SplitOverlayBackend::Pause(AppState& app) -> Result<> {
    return renderer_->Pause(app);
}

auto SplitOverlayBackend::Resume(AppState& app) -> Result<> {
    return renderer_->Resume(app);
}

auto SplitOverlayBackend::ShouldRender(AppState& app) -> Result<RenderReadiness> {
    return renderer_->ShouldRender(app);
}

auto SplitOverlayBackend::Render(AppState& app, const std::shared_ptr<Image>& target, CommandBatch& batch, float alpha)
    -> Result<bool> {
    return renderer_->Render(app, target, batch, alpha);
}

auto SplitOverlayBackend::Meta() -> std::optional<FrameMeta> {
    return renderer_->Meta();
}

auto SplitOverlayBackend::SetTexelFootprint(const TexelFootprint& footprint) -> void {
    renderer_->SetTexelFootprint(footprint);
}

auto SplitOverlayBackend::OnHover(AppState& app, const PointerHit& hit) -> std::optional<Haptics> {
    return interaction_->OnHover(app, hit);
}

auto SplitOverlayBackend::OnLeft(AppState& app, size_t pointer) -> void {
    interaction_->OnLeft(app, pointer);
}

auto SplitOverlayBackend::OnPointer(AppState& app, const PointerHit& hit, bool pressed) -> void {
    interaction_->OnPointer(app, hit, pressed);
}

auto SplitOverlayBackend::OnScroll(AppState& app, const PointerHit& hit, float delta_y, float delta_x) -> void {
    interaction_->OnScroll(app, hit, delta_y, delta_x);
}

auto OverlayState::NextId() -> OverlayId {
    static std::atomic<uint64_t> next{0};
    return static_cast<OverlayId>(next.fetch_add(1, std::memory_order_relaxed));
}

auto OverlayState::UiTransform(VkExtent2D extent) -> Eigen::Affine2f {
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const Eigen::Vector2f scale = aspect < 1.0f ? Eigen::Vector2f(1.0f / aspect, -1.0f) : Eigen::Vector2f(1.0f, -aspect);
    Eigen::Affine2f transform = Eigen::Affine2f::Identity();
    transform.linear() = scale.asDiagonal();
    transform.translation() = Eigen::Vector2f(0.5f, 0.5f);
    return transform;
}

auto OverlayState::ParentTransform(const AppState& app) const -> std::optional<Eigen::Affine3f> {
    switch (relative_to) {
    case RelativeTo::Head: return app.input.hmd;
    case RelativeTo::LeftHand: return app.input.pointers[0].tracked_pose;
    case RelativeTo::RightHand: return app.input.pointers[1].tracked_pose;
    case RelativeTo::None:
    case RelativeTo::Stage: return std::nullopt;
    }
    return std::nullopt;
}

auto OverlayState::LocalTransform() const -> Eigen::Affine3f {
    if (saved_transform) {
        return *saved_transform;
    }
    Eigen::Affine3f local = Eigen::Affine3f::Identity();
    local.linear() = spawn_rotation.toRotationMatrix() * spawn_scale;
    local.translation() = spawn_point;
    return local;
}

auto OverlayState::AutoMovement(const AppState& app) -> void {
    if (const auto parent = ParentTransform(app)) {
        transform = *parent * LocalTransform();
        dirty = true;
    }
}

auto OverlayState::Reset(const AppState& app, bool hard_reset) -> void {
    if (hard_reset) {
        saved_transform.reset();
    }
    transform = ParentTransform(app).value_or(app.input.hmd) * LocalTransform();
    dirty = true;
}

auto OverlayState::RememberRelativeToHead(const AppState& app) -> Eigen::Affine3f {
    const Eigen::Affine3f relative(app.input.hmd.matrix().inverse() * transform.matrix());
    saved_transform = relative;
    return relative;
}

auto OverlayRenderer::SetTexelFootprint(const TexelFootprint&) -> void {
}

Overlay::Overlay(OverlayState overlay_state, std::unique_ptr<OverlayBackend> overlay_backend)
    : state(std::move(overlay_state)),
      backend(overlay_backend ? std::move(overlay_backend) : std::make_unique<SplitOverlayBackend>()) {
}

auto Overlay::Init(AppState& app) -> Result<> {
    if (!state.curvature) {
        for (const auto& [name, value] : app.config.curve_values) {
            if (name == state.name) {
                state.curvature = value;
            }
        }
    }

    if (state.relative_to == RelativeTo::None) {
        bool hard_reset = true;
        for (const auto& [name, transform] : app.config.transform_values) {
            if (name == state.name) {
                state.saved_transform = transform;
                hard_reset = false;
            }
        }
        state.Reset(app, hard_reset);
    }
    return backend->Init(app);
}
