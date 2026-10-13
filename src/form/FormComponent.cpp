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

#include "FormComponent.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

FormComponent::FormComponent(std::string name) : m_name(std::move(name)) {
}

auto FormComponent::SetRect(const CanvasRect& rect) -> void {
    m_position = {rect.x, rect.y};
    m_size = {rect.w, rect.h};
}

auto FormComponent::Attach(std::unique_ptr<FormComponent> child) -> void {
    child->m_parent = this;
    m_children.push_back(std::move(child));
    m_dirty = true;
}

auto FormComponent::Find(std::string_view name) -> FormComponent* {
    if (m_name == name) {
        return this;
    }
    for (const auto& child : m_children) {
        if (auto* found = child->Find(name); found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

auto FormComponent::Clear() -> void {
    m_children.clear();
    m_dirty = true;
}

auto FormComponent::Build(FormState& state, const Eigen::Vector2f& origin, float scale, Rounding rounding) -> void {
    Layout();
    const FormFrame frame{
        .rect =
            {
                std::fma(m_position.x(), scale, origin.x()),
                std::fma(m_position.y(), scale, origin.y()),
                m_size.x() * scale,
                m_size.y() * scale,
            },
        .scale = scale,
        .rounding = rounding,
    };
    Emit(state, frame);

    std::vector<size_t> order(m_children.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::ranges::stable_sort(order, [this](size_t a, size_t b) -> bool { return m_children[a]->m_z < m_children[b]->m_z; });
    const Eigen::Vector2f child_origin(frame.rect.x, frame.rect.y);
    const float child_scale = scale * m_scale;
    for (const auto index : order) {
        m_children[index]->Build(state, child_origin, child_scale, rounding);
    }
}

auto FormComponent::UpdateAll() -> bool {
    Update();
    bool dirty = std::exchange(m_dirty, false);
    for (const auto& child : m_children) {
        dirty = child->UpdateAll() || dirty;
    }
    return dirty;
}

auto FormComponent::OnPress(PointerMode mode) -> void {
    if (m_on_press) {
        m_on_press(mode);
    }
}

auto FormComponent::OnRelease() -> void {
    if (m_on_release) {
        m_on_release();
    }
}

auto FormComponent::OnScroll(float delta_y, float delta_x) -> void {
    if (m_on_scroll) {
        m_on_scroll(delta_y, delta_x);
    }
}

auto FormComponent::HighlightColor() const -> std::optional<Eigen::Vector4f> {
    return m_highlight_color ? m_highlight_color() : std::nullopt;
}

auto FormComponent::Outline() const -> std::optional<HighlightOutline> {
    return m_outline ? m_outline() : std::nullopt;
}

auto FormComponent::Update() -> void {
}

auto FormComponent::Layout() -> void {
}

auto FormComponent::Emit(FormState&, const FormFrame&) -> void {
}
