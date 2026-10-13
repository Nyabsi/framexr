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

#include "RowComponent.hpp"

#include <utility>

RowComponent::RowComponent(std::string name, float height, float gap) : FormComponent(std::move(name)), m_gap(gap) {
    SetSize({0.0f, height});
}

auto RowComponent::SetWeights(std::vector<float> weights) -> void {
    m_weights = std::move(weights);
    MarkDirty();
}

auto RowComponent::Layout() -> void {
    const auto& children = Children();
    if (children.empty()) {
        return;
    }
    const auto weight = [this](size_t index) -> float { return index < m_weights.size() ? m_weights[index] : 1.0f; };
    float total = 0.0f;
    for (size_t i = 0; i < children.size(); ++i) {
        total += weight(i);
    }
    const float available = Size().x() - m_gap * static_cast<float>(children.size() - 1);
    float x = 0.0f;
    for (size_t i = 0; i < children.size(); ++i) {
        const float width = available * weight(i) / total;
        children[i]->SetRect({x, 0.0f, width, Size().y()});
        x += width + m_gap;
    }
}
