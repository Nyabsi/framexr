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

#include "FormDesigner.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <ranges>
#include <utility>

#include "EditorColors.hpp"
#include "Font.hpp"
#include "Label.hpp"
#include "OutlineControl.hpp"
#include "Theme.hpp"

namespace {

constexpr float kPreviewStep = 32.0f;
constexpr float kMoveThreshold = 2.0f;
constexpr float kSnapRelease = 12.0f;
constexpr float kLabelHeight = 1.3f;
constexpr float kCityGap = 6.0f;
constexpr float kMinFormWidth = 60.0f;
constexpr float kMinFormHeight = 40.0f;
constexpr float kMaxFormSize = 1600.0f;
constexpr float kMinToolbarHeight = 20.0f;
constexpr float kMaxToolbarHeight = 160.0f;
constexpr float kNewFormWidth = 200.0f;
constexpr float kNewFormHeight = 120.0f;

auto Contains(const CanvasRect& rect, const Eigen::Vector2f& point, float slack = 0.0f) -> bool {
    return point.x() >= rect.x - slack && point.x() <= rect.x + rect.w + slack && point.y() >= rect.y - slack
        && point.y() <= rect.y + rect.h + slack;
}

auto Overlaps(const CanvasRect& a, const CanvasRect& b) -> bool {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

auto CenteredRect(float x, float y, float w, float h) -> CanvasRect {
    return {x - w / 2.0f, y - h / 2.0f, w, h};
}

auto BaselineLabel(const ModularElement& element) -> const LabelElement* {
    const auto* label = std::get_if<LabelElement>(&element);
    return label != nullptr && !label->centered ? label : nullptr;
}

auto CityAbove(const LabelElement& label) -> float {
    const auto* clock = std::get_if<ClockLabelContent>(&label.content);
    if (clock == nullptr || !clock->city_size) {
        return 0.0f;
    }
    const auto city = static_cast<float>(*clock->city_size);
    return label.centered ? city * kLabelHeight : city + kCityGap;
}

auto FontSize(const ModularElement& element) -> int {
    return std::visit(
        [](const auto& item) -> int {
            if constexpr (requires { item.font_size; }) {
                return item.font_size;
            } else {
                return 0;
            }
        },
        element
    );
}

auto SetFontSize(ModularElement& element, int font_size) -> void {
    std::visit(
        [font_size](auto& item) -> void {
            if constexpr (requires { item.font_size; }) {
                item.font_size = font_size;
            }
        },
        element
    );
}

auto Outline(const Eigen::Vector4f& color, float width) -> std::unique_ptr<OutlineControl> {
    return std::make_unique<OutlineControl>(std::nullopt, HighlightOutline{.color = color, .width = width});
}

auto WithAlpha(Eigen::Vector4f color, float alpha) -> Eigen::Vector4f {
    color.w() = alpha;
    return color;
}

auto FormSize(const ModularUiConfig& config, size_t form) -> Eigen::Vector2f {
    if (form == 0 || form > config.Forms().size()) {
        return {config.Form().width, config.Form().height};
    }
    const auto& extra = config.Forms()[form - 1];
    return {extra.width, extra.height};
}

auto SetFormSize(ModularUiConfig& config, size_t form, const Eigen::Vector2f& size) -> void {
    if (form == 0 || form > config.Forms().size()) {
        config.Form().width = size.x();
        config.Form().height = size.y();
        return;
    }
    config.Forms()[form - 1].width = size.x();
    config.Forms()[form - 1].height = size.y();
}

} // namespace

FormDesigner::FormDesigner(ModularUiConfig config) : m_revisions(std::move(config)) {
}

auto FormDesigner::Origin() const -> Eigen::Vector2f {
    return m_frozen_origin.value_or(Document().Origin());
}

auto FormDesigner::Form(size_t index) const -> CanvasRect {
    return Document().FormRect(index, Origin());
}

auto FormDesigner::Toolbar() const -> std::optional<CanvasRect> {
    return Document().ToolbarRect(Origin());
}

auto FormDesigner::FormAt(const Eigen::Vector2f& point) const -> std::optional<size_t> {
    const auto count = Document().FormCount();
    for (size_t offset = 0; offset < count; ++offset) {
        const size_t index = count - 1 - offset;
        if (Contains(Form(index), point)) {
            return index;
        }
    }
    return std::nullopt;
}

auto FormDesigner::Extent() const -> Eigen::Vector2f {
    Eigen::Vector2f extent = Eigen::Vector2f::Zero();
    for (size_t index = 0; index < Document().FormCount(); ++index) {
        const auto rect = Form(index);
        extent = extent.cwiseMax(Eigen::Vector2f(rect.x + rect.w, rect.y + rect.h));
    }
    if (const auto toolbar = Toolbar()) {
        extent = extent.cwiseMax(Eigen::Vector2f(toolbar->x + toolbar->w, toolbar->y + toolbar->h));
    }
    return extent;
}

auto FormDesigner::PreviewSize() const -> Eigen::Vector2f {
    Eigen::Vector2f document = Extent();
    if (m_drag && m_drag->hit.kind != FormHitKind::Element && m_drag->hit.kind != FormHitKind::Resize) {
        const auto start = m_drag->before.Size();
        document = document.cwiseMax(Eigen::Vector2f(static_cast<float>(start[0]), static_cast<float>(start[1])));
    }
    const Eigen::Vector2f preview = (document + Eigen::Vector2f::Constant(2.0f * kPad)) * kScale;
    return (preview / kPreviewStep).array().ceil().matrix() * kPreviewStep;
}

auto FormDesigner::ToDocument(const Eigen::Vector2f& preview) const -> Eigen::Vector2f {
    return preview / kScale - Eigen::Vector2f::Constant(kPad);
}

auto FormDesigner::ToPreview(const Eigen::Vector2f& document) const -> Eigen::Vector2f {
    return (document + Eigen::Vector2f::Constant(kPad)) * kScale;
}

auto FormDesigner::VisualRect(size_t index) const -> CanvasRect {
    const auto arranged = Document().Arrange(Origin());
    const auto& element = arranged.at(index);
    const auto& [x, y, w, h] = ModularElements::Rect(element);
    if (const auto* label = BaselineLabel(element)) {
        const auto font = static_cast<float>(label->font_size);
        const float above = CityAbove(*label);
        return {x, y - font - above, MinimumSize(element).x(), font * kLabelHeight + above};
    }
    if (const auto* label = std::get_if<LabelElement>(&element)) {
        const float above = CityAbove(*label);
        return {x, y - above, w, h + above};
    }
    return {x, y, w, h};
}

auto FormDesigner::Handle(FormHitKind kind, size_t form) const -> std::optional<CanvasRect> {
    switch (kind) {
    case FormHitKind::FormCorner: {
        const auto rect = Form(form);
        return CenteredRect(rect.x + rect.w, rect.y + rect.h, kHandle, kHandle);
    }
    case FormHitKind::ToolbarCorner: {
        const auto& config = Document();
        const auto toolbar = Toolbar();
        if (!toolbar || !config.Form().toolbar) {
            return std::nullopt;
        }
        const bool below = config.Form().toolbar->position == ToolbarPosition::Below;
        return CenteredRect(toolbar->x + toolbar->w, below ? toolbar->y + toolbar->h : toolbar->y, kHandle, kHandle);
    }
    case FormHitKind::None:
    case FormHitKind::Element:
    case FormHitKind::Resize:
    case FormHitKind::FormBody: break;
    }
    return std::nullopt;
}

auto FormDesigner::MinimumForm(const ModularUiConfig& config, size_t form) const -> Eigen::Vector2f {
    Eigen::Vector2f minimum(kMinFormWidth, kMinFormHeight);
    float toolbar_width = 2.0f;
    for (const auto& element : config.Elements()) {
        if (ModularElements::RegionOf(element) == Region::Toolbar) {
            toolbar_width += MinimumSize(element).x() + 2.0f;
            continue;
        }
        if (ModularElements::FormOf(element) != form) {
            continue;
        }
        const auto& [x, y, w, h] = ModularElements::Rect(element);
        if (const auto* label = BaselineLabel(element)) {
            const auto font = static_cast<float>(label->font_size);
            minimum = minimum.cwiseMax(Eigen::Vector2f(x + MinimumSize(element).x(), y - font + font * kLabelHeight));
        } else {
            minimum = minimum.cwiseMax(Eigen::Vector2f(x + w, y + h));
        }
    }
    if (form == 0 && config.Form().toolbar) {
        minimum.x() = std::max(minimum.x(), toolbar_width);
    }
    return minimum;
}

auto FormDesigner::Fit(ModularUiConfig& config, size_t index) const -> void {
    auto& element = config.Elements().at(index);
    auto& rect = ModularElements::Rect(element);
    const auto minimum = MinimumSize(element);
    if (ModularElements::RegionOf(element) == Region::Form) {
        const auto size = FormSize(config, ModularElements::FormOf(element));
        rect[2] = std::min(rect[2], size.x() - rect[0]);
        rect[2] = std::max(rect[2], minimum.x());
        if (BaselineLabel(element) == nullptr) {
            rect[3] = std::clamp(rect[3], minimum.y(), std::max(size.y() - rect[1], minimum.y()));
        }
    } else {
        rect[2] = std::max(rect[2], minimum.x());
    }
    if (auto& toolbar = config.Form().toolbar) {
        toolbar->height = std::max(toolbar->height, MinimumToolbarHeight(config));
    }
    for (size_t form = 0; form < config.FormCount(); ++form) {
        SetFormSize(config, form, FormSize(config, form).cwiseMax(MinimumForm(config, form)));
    }
}

auto FormDesigner::Accepts(const Eigen::Vector2f& point, int layer) const -> bool {
    return layer == 0
        || (m_drag && m_drag->hit.kind == FormHitKind::Element && LayerOf(Document().Elements().at(m_drag->hit.index)) == layer)
        || HitTest(point, layer).kind != FormHitKind::None;
}

auto FormDesigner::HitTest(const Eigen::Vector2f& point, std::optional<int> layer) const -> FormHit {
    const auto& config = Document();
    const auto& elements = config.Elements();
    const auto in_layer = [&](size_t index) -> bool { return !layer || LayerOf(elements.at(index)) == *layer; };
    const bool back = layer.value_or(0) == 0;
    if (m_selected && *m_selected < elements.size() && in_layer(*m_selected)) {
        const auto rect = VisualRect(*m_selected);
        if (Contains(CenteredRect(rect.x + rect.w, rect.y + rect.h, kHandle, kHandle), point, 2.0f)) {
            return {.kind = FormHitKind::Resize, .index = *m_selected};
        }
    }
    if (back) {
        for (size_t form = 0; form < config.FormCount(); ++form) {
            if (const auto handle = Handle(FormHitKind::FormCorner, form); handle && Contains(*handle, point, 2.0f)) {
                return {.kind = FormHitKind::FormCorner, .index = form};
            }
        }
        if (const auto handle = Handle(FormHitKind::ToolbarCorner, 0); handle && Contains(*handle, point, 2.0f)) {
            return {.kind = FormHitKind::ToolbarCorner};
        }
    }
    const auto count = elements.size();
    for (size_t offset = 0; offset < count; ++offset) {
        const size_t index = count - 1 - offset;
        if (in_layer(index) && Contains(VisualRect(index), point, 1.0f)) {
            return {.kind = FormHitKind::Element, .index = index};
        }
    }
    if (back) {
        if (const auto form = FormAt(point)) {
            return {.kind = FormHitKind::FormBody, .index = *form};
        }
    }
    return {};
}

auto FormDesigner::InToolbar(const Eigen::Vector2f& point) const -> bool {
    const auto toolbar = Toolbar();
    return toolbar && Contains(*toolbar, point);
}

auto FormDesigner::Hover(const Eigen::Vector2f& point) -> void {
    if (m_drag) {
        Move(point);
        return;
    }
    const auto hit = HitTest(point);
    m_hovered = hit.kind == FormHitKind::Element ? std::optional(hit.index) : std::nullopt;
    if (m_carrying) {
        m_carrying->second = point;
    }
}

auto FormDesigner::Press(const Eigen::Vector2f& point, std::optional<int> layer) -> void {
    const auto hit = HitTest(point, layer);
    const auto& config = Document();
    const bool reselected = (hit.kind == FormHitKind::Element && m_selected == hit.index)
                         || (hit.kind == FormHitKind::FormBody && m_selected_form == hit.index);
    switch (hit.kind) {
    case FormHitKind::None:
        m_selected.reset();
        m_selected_form.reset();
        return;
    case FormHitKind::Element:
        m_selected = hit.index;
        m_selected_form.reset();
        break;
    case FormHitKind::FormBody:
        m_selected.reset();
        m_selected_form = hit.index;
        if (hit.index == 0) {
            return;
        }
        break;
    case FormHitKind::Resize:
    case FormHitKind::FormCorner:
    case FormHitKind::ToolbarCorner: break;
    }
    Drag drag{
        .hit = hit,
        .start = point,
        .before = config,
        .engaged = m_hold.count() == 0,
        .reselected = reselected,
        .pressed_at = std::chrono::steady_clock::now(),
    };
    switch (hit.kind) {
    case FormHitKind::Element:
    case FormHitKind::Resize: {
        const auto& element = config.Elements().at(hit.index);
        drag.rect = ModularElements::Rect(element);
        drag.font_size = FontSize(element);
        const auto arranged = config.Arrange(Origin());
        const auto& placed = ModularElements::Rect(arranged.at(hit.index));
        drag.start_position = {placed[0], placed[1]};
        drag.form = ModularElements::FormOf(element);
        break;
    }
    case FormHitKind::FormBody: {
        const auto& extra = config.Forms().at(hit.index - 1);
        drag.rect = {extra.x, extra.y, extra.width, extra.height};
        m_frozen_origin = config.Origin();
        break;
    }
    case FormHitKind::FormCorner: {
        const auto size = FormSize(config, hit.index);
        drag.rect = {size.x(), size.y(), 0.0f, 0.0f};
        break;
    }
    case FormHitKind::ToolbarCorner:
        drag.rect = {0.0f, 0.0f, config.Form().toolbar ? config.Form().toolbar->height : 0.0f, 0.0f};
        m_frozen_origin = config.Origin();
        break;
    case FormHitKind::None: break;
    }
    m_snap_x = {};
    m_snap_y = {};
    m_drag = std::move(drag);
}

auto FormDesigner::Move(const Eigen::Vector2f& point) -> void {
    if (!m_drag) {
        return;
    }
    if (!m_drag->engaged) {
        if (std::chrono::steady_clock::now() - m_drag->pressed_at < m_hold) {
            return;
        }
        m_drag->engaged = true;
        m_drag->start = point;
        return;
    }
    if ((point - m_drag->start).norm() > kMoveThreshold) {
        m_drag->moved = true;
    }
    if (!m_drag->moved) {
        return;
    }
    m_guides.clear();
    m_toolbar_marker.reset();
    m_landing.reset();
    auto& drag = *m_drag;
    switch (drag.hit.kind) {
    case FormHitKind::Element: MoveElement(drag, point); break;
    case FormHitKind::Resize: ResizeElement(drag, point); break;
    case FormHitKind::FormBody: MoveForm(drag, point); break;
    case FormHitKind::FormCorner:
    case FormHitKind::ToolbarCorner: ResizeRegion(drag, point); break;
    case FormHitKind::None: break;
    }
}

auto FormDesigner::SnapAxis(float start, float size, std::span<const float> targets, bool vertical, float offset, SnapLock& lock)
    -> float {
    const std::array edges{start, start + size / 2.0f, start + size};
    if (!m_snap) {
        lock = {};
        return 0.0f;
    }
    if (lock.active) {
        if (const float distance = lock.target - edges.at(lock.edge); std::abs(distance) < kSnapRelease) {
            m_guides.push_back({.vertical = vertical, .position = lock.target + offset});
            return distance;
        }
        lock = {};
    }
    float best = std::numeric_limits<float>::max();
    for (size_t edge = 0; edge < edges.size(); ++edge) {
        for (const float target : targets) {
            if (const float distance = target - edges.at(edge); std::abs(distance) < kSnap && std::abs(distance) < std::abs(best)) {
                best = distance;
                lock = {.active = true, .edge = edge, .target = target};
            }
        }
    }
    if (!lock.active) {
        return 0.0f;
    }
    m_guides.push_back({.vertical = vertical, .position = lock.target + offset});
    return best;
}

auto FormDesigner::MoveElement(Drag& drag, const Eigen::Vector2f& point) -> void {
    auto& config = m_revisions.Live();
    const auto index = drag.hit.index;
    auto& element = config.Elements().at(index);

    if (ModularElements::RegionOf(element) == Region::Toolbar) {
        if (const auto toolbar_rect = Toolbar(); toolbar_rect && InToolbar(point)) {
            const auto arranged = config.Arrange(Origin());
            const auto order = ToolbarOrder(index);
            const auto position = InsertionIndex(point.x(), index);
            if (position < order.size()) {
                m_toolbar_marker = ModularElements::Rect(arranged[order[position]])[0] - 2.0f;
            } else if (!order.empty()) {
                const auto& last = ModularElements::Rect(arranged[order.back()]);
                m_toolbar_marker = last[0] + last[2] + 2.0f;
            } else {
                m_toolbar_marker = toolbar_rect->x + 4.0f;
            }
        } else {
            const auto rect = VisualRect(index);
            m_landing = CenteredRect(point.x(), point.y(), rect.w, rect.h);
        }
        return;
    }

    auto& rect = ModularElements::Rect(element);
    if (InToolbar(point)) {
        ModularElements::SetForm(element, ModularElements::FormOf(drag.before.Elements().at(index)));
        rect = drag.rect;
        m_drop_form.reset();
        const auto visual = VisualRect(index);
        m_landing = CenteredRect(point.x(), point.y(), visual.w, visual.h);
        return;
    }

    const auto over = FormAt(point);
    if (over && *over != drag.form) {
        drag.form = *over;
        m_snap_x = {};
        m_snap_y = {};
    }
    const auto form = Form(drag.form);
    ModularElements::SetForm(element, drag.form);
    m_drop_form = drag.form;
    const Eigen::Vector2f position = drag.start_position + (point - drag.start) - Eigen::Vector2f(form.x, form.y);
    rect = drag.rect;
    rect[0] = position.x();
    rect[1] = position.y();

    const auto visual = VisualRect(index);
    const float visual_dy = visual.y - form.y - rect[1];
    if (over) {
        std::vector<float> xs{0.0f, form.w / 2.0f, form.w};
        std::vector<float> ys{0.0f, form.h / 2.0f, form.h};
        for (size_t i = 0; i < config.Elements().size(); ++i) {
            const auto& other_element = config.Elements()[i];
            if (i == index || ModularElements::RegionOf(other_element) != Region::Form
                || ModularElements::FormOf(other_element) != drag.form) {
                continue;
            }
            const auto other = VisualRect(i);
            xs.insert(xs.end(), {other.x - form.x, other.x - form.x + other.w / 2.0f, other.x - form.x + other.w});
            ys.insert(ys.end(), {other.y - form.y, other.y - form.y + other.h / 2.0f, other.y - form.y + other.h});
        }
        rect[0] += SnapAxis(rect[0], visual.w, xs, true, form.x, m_snap_x);
        rect[1] += SnapAxis(rect[1] + visual_dy, visual.h, ys, false, form.y, m_snap_y);
    } else {
        m_snap_x = {};
        m_snap_y = {};
    }
    const float x = std::clamp(rect[0], 0.0f, std::max(form.w - visual.w, 0.0f));
    const float y = std::clamp(rect[1] + visual_dy, 0.0f, std::max(form.h - visual.h, 0.0f));
    if (std::abs(x - rect[0]) > 0.5f || std::abs(y - rect[1] - visual_dy) > 0.5f) {
        m_landing = CanvasRect{form.x + x, form.y + y, visual.w, visual.h};
    }
}

auto FormDesigner::ClampIntoForm(size_t index) -> void {
    auto& config = m_revisions.Live();
    auto& element = config.Elements().at(index);
    if (ModularElements::RegionOf(element) != Region::Form) {
        return;
    }
    auto& rect = ModularElements::Rect(element);
    const auto form = Form(ModularElements::FormOf(element));
    const auto visual = VisualRect(index);
    const float visual_dy = visual.y - form.y - rect[1];
    rect[0] = std::clamp(rect[0], 0.0f, std::max(form.w - visual.w, 0.0f));
    rect[1] = std::clamp(rect[1] + visual_dy, 0.0f, std::max(form.h - visual.h, 0.0f)) - visual_dy;
}

auto FormDesigner::MoveForm(const Drag& drag, const Eigen::Vector2f& point) -> void {
    auto& config = m_revisions.Live();
    const size_t index = drag.hit.index;
    auto& extra = config.Forms().at(index - 1);
    const Eigen::Vector2f origin = Origin();
    const Eigen::Vector2f delta = point - drag.start;
    float x = drag.rect[0] + delta.x();
    float y = drag.rect[1] + delta.y();

    std::vector<float> xs;
    std::vector<float> ys;
    for (size_t other = 0; other < config.FormCount(); ++other) {
        if (other == index) {
            continue;
        }
        const auto rect = config.FormRect(other, Eigen::Vector2f::Zero());
        xs.insert(xs.end(), {rect.x, rect.x + rect.w, rect.x + rect.w + kFormGap, rect.x - kFormGap, rect.x + rect.w / 2.0f});
        ys.insert(ys.end(), {rect.y, rect.y + rect.h, rect.y + rect.h + kFormGap, rect.y - kFormGap, rect.y + rect.h / 2.0f});
    }
    x += SnapAxis(x, extra.width, xs, true, origin.x(), m_snap_x);
    y += SnapAxis(y, extra.height, ys, false, origin.y(), m_snap_y);
    extra.x = std::clamp(x, -origin.x() - kPad + 4.0f, kMaxFormSize);
    extra.y = std::clamp(y, -origin.y() - kPad + 4.0f, kMaxFormSize);
}

auto FormDesigner::ResizeElement(const Drag& drag, const Eigen::Vector2f& point) -> void {
    auto& config = m_revisions.Live();
    const auto index = drag.hit.index;
    auto& element = config.Elements().at(index);
    auto& rect = ModularElements::Rect(element);
    const Eigen::Vector2f delta = point - drag.start;
    rect = drag.rect;
    if (BaselineLabel(element) != nullptr) {
        SetFontSize(element, drag.font_size);
    }
    if (ModularElements::RegionOf(element) == Region::Toolbar) {
        rect[2] = std::max(drag.rect[2] + delta.x(), MinimumSize(element).x());
        return;
    }
    const auto form_index = ModularElements::FormOf(element);
    const auto form = Form(form_index);
    if (const auto* label = BaselineLabel(element)) {
        const float above = CityAbove(*label);
        const float start_height = static_cast<float>(drag.font_size) * kLabelHeight;
        const float top = rect[1] - static_cast<float>(drag.font_size) - above;
        const float room = std::max(form.h - top - above, 6.0f * kLabelHeight);
        const float height = std::clamp(start_height + delta.y(), 6.0f * kLabelHeight, room);
        SetFontSize(element, std::clamp(static_cast<int>(std::floor(height / kLabelHeight)), 6, 200));
        rect[1] = top + above + static_cast<float>(FontSize(element));
        while (FontSize(element) > 6 && rect[0] + MinimumSize(element).x() > form.w) {
            SetFontSize(element, FontSize(element) - 1);
            rect[1] = top + above + static_cast<float>(FontSize(element));
        }
        return;
    }
    rect[2] = drag.rect[2] + delta.x();
    rect[3] = drag.rect[3] + delta.y();
    std::vector<float> xs{form.w / 2.0f, form.w};
    std::vector<float> ys{form.h / 2.0f, form.h};
    for (size_t i = 0; i < config.Elements().size(); ++i) {
        const auto& other_element = config.Elements()[i];
        if (i != index && ModularElements::RegionOf(other_element) == Region::Form
            && ModularElements::FormOf(other_element) == form_index) {
            const auto other = VisualRect(i);
            xs.insert(xs.end(), {other.x - form.x, other.x - form.x + other.w});
            ys.insert(ys.end(), {other.y - form.y, other.y - form.y + other.h});
        }
    }
    rect[2] += SnapAxis(rect[0] + rect[2], 0.0f, xs, true, form.x, m_snap_x);
    rect[3] += SnapAxis(rect[1] + rect[3], 0.0f, ys, false, form.y, m_snap_y);
    const auto minimum = MinimumSize(element);
    rect[2] = std::clamp(rect[2], minimum.x(), std::max(form.w - rect[0], minimum.x()));
    rect[3] = std::clamp(rect[3], minimum.y(), std::max(form.h - rect[1], minimum.y()));
}

auto FormDesigner::ResizeRegion(const Drag& drag, const Eigen::Vector2f& point) -> void {
    auto& config = m_revisions.Live();
    const Eigen::Vector2f delta = point - drag.start;
    const auto rounded = [this](float value, float step) -> float { return m_snap ? std::round(value / step) * step : value; };
    switch (drag.hit.kind) {
    case FormHitKind::FormCorner: {
        const auto minimum = MinimumForm(drag.before, drag.hit.index);
        SetFormSize(
            config,
            drag.hit.index,
            {std::clamp(rounded(drag.rect[0] + delta.x(), 10.0f), minimum.x(), kMaxFormSize),
             std::clamp(rounded(drag.rect[1] + delta.y(), 10.0f), minimum.y(), kMaxFormSize)}
        );
        break;
    }
    case FormHitKind::ToolbarCorner:
        if (auto& toolbar = config.Form().toolbar) {
            const float change = toolbar->position == ToolbarPosition::Below ? delta.y() : -delta.y();
            toolbar->height = std::clamp(rounded(drag.rect[2] + change, 2.0f), MinimumToolbarHeight(drag.before), kMaxToolbarHeight);
        }
        break;
    case FormHitKind::None:
    case FormHitKind::Element:
    case FormHitKind::Resize:
    case FormHitKind::FormBody: break;
    }
}

auto FormDesigner::PlaceInForm(size_t index, const Eigen::Vector2f& center) -> void {
    auto& config = m_revisions.Live();
    auto& element = config.Elements().at(index);
    const auto target = FormAt(center).value_or(0);
    const auto form = Form(target);
    auto& rect = ModularElements::Rect(element);
    if (rect[3] <= 0.0f) {
        const auto& toolbar = config.Form().toolbar;
        rect[3] = toolbar ? toolbar->height - 2.0f * toolbar->padding : 32.0f;
    }
    ModularElements::SetRegion(element, Region::Form);
    ModularElements::SetForm(element, target);
    const auto minimum = MinimumSize(element);
    rect[2] = std::max(std::min(rect[2], form.w), minimum.x());
    rect[3] = std::max(std::min(rect[3], form.h), minimum.y());
    rect[0] = std::clamp(center.x() - form.x - rect[2] / 2.0f, 0.0f, std::max(form.w - rect[2], 0.0f));
    rect[1] = std::clamp(center.y() - form.y - rect[3] / 2.0f, 0.0f, std::max(form.h - rect[3], 0.0f));
    if (const auto* label = BaselineLabel(element)) {
        const auto font = static_cast<float>(label->font_size);
        const float above = CityAbove(*label);
        const float height = font * kLabelHeight + above;
        const float width = MinimumSize(element).x();
        rect[0] = std::clamp(center.x() - form.x - width / 2.0f, 0.0f, std::max(form.w - width, 0.0f));
        rect[1] = std::clamp(center.y() - form.y - height / 2.0f, 0.0f, std::max(form.h - height, 0.0f)) + above + font;
    }
}

auto FormDesigner::Release(const Eigen::Vector2f& point) -> bool {
    if (!m_drag) {
        return false;
    }
    auto drag = std::move(*m_drag);
    m_drag.reset();
    m_guides.clear();
    m_toolbar_marker.reset();
    m_landing.reset();
    m_frozen_origin.reset();
    m_drop_form.reset();
    m_snap_x = {};
    m_snap_y = {};
    if (!drag.moved) {
        if (drag.reselected) {
            m_selected.reset();
            m_selected_form.reset();
        }
        return false;
    }
    if (!drag.engaged) {
        return false;
    }
    if (drag.hit.kind == FormHitKind::Element) {
        const auto index = drag.hit.index;
        const bool in_toolbar = ModularElements::RegionOf(Document().Elements().at(index)) == Region::Toolbar;
        if (InToolbar(point)) {
            m_selected = PlaceInToolbar(index, InsertionIndex(point.x(), index));
        } else if (in_toolbar) {
            PlaceInForm(index, point);
        } else {
            ClampIntoForm(index);
        }
    }
    m_revisions.Commit(std::move(drag.before));
    return true;
}

auto FormDesigner::InsertForm(const std::optional<Eigen::Vector2f>& point) -> void {
    auto before = Document();
    auto& config = m_revisions.Live();
    const Eigen::Vector2f origin = config.Origin();
    CanvasRect rect{config.Form().width + kFormGap, 0.0f, kNewFormWidth, kNewFormHeight};
    if (point) {
        rect.x = point->x() - origin.x() - kNewFormWidth / 2.0f;
        rect.y = point->y() - origin.y() - kNewFormHeight / 2.0f;
    }
    const auto overlapping = [&config](const CanvasRect& candidate) -> bool {
        for (size_t form = 0; form < config.FormCount(); ++form) {
            if (Overlaps(candidate, config.FormRect(form, Eigen::Vector2f::Zero()))) {
                return true;
            }
        }
        return false;
    };
    if (overlapping(rect)) {
        float right = 0.0f;
        for (size_t form = 0; form < config.FormCount(); ++form) {
            const auto other = config.FormRect(form, Eigen::Vector2f::Zero());
            right = std::max(right, other.x + other.w);
        }
        rect = {right + kFormGap, 0.0f, kNewFormWidth, kNewFormHeight};
    }
    config.Forms().push_back({.x = rect.x, .y = rect.y, .width = rect.w, .height = rect.h, .bg_color = "@background"});
    m_revisions.Commit(std::move(before));
    m_selected.reset();
    m_selected_form = Document().FormCount() - 1;
}

auto FormDesigner::Insert(FormPart part, const std::optional<Eigen::Vector2f>& point) -> std::optional<size_t> {
    if (part == FormPart::Form) {
        InsertForm(point);
        return std::nullopt;
    }
    auto before = Document();
    auto& config = m_revisions.Live();
    config.Elements().push_back(MakePart(part));
    size_t index = config.Elements().size() - 1;
    const auto form = Form(0);
    const Eigen::Vector2f center = point.value_or(Eigen::Vector2f(form.x + form.w / 2.0f, form.y + form.h / 2.0f));
    if (InToolbar(center)) {
        index = PlaceInToolbar(index, InsertionIndex(center.x(), index));
    } else {
        PlaceInForm(index, center);
    }
    m_revisions.Commit(std::move(before));
    m_selected = index;
    m_selected_form.reset();
    return index;
}

auto FormDesigner::RemoveForm(size_t form) -> void {
    if (form == 0 || form >= Document().FormCount()) {
        return;
    }
    m_revisions.Edit([form](ModularUiConfig& config) -> void { config.RemoveForm(form); });
    m_selected.reset();
    m_selected_form.reset();
    m_hovered.reset();
}

auto FormDesigner::MinimumSize(const ModularElement& element) const -> Eigen::Vector2f {
    const auto text_width = [this](std::string_view text, int font_size) -> float {
        if (measure) {
            return measure(text, font_size);
        }
        return 0.6f * static_cast<float>(font_size) * static_cast<float>(FontCache::DecodeUtf8(text).size());
    };
    return std::visit(
        [&](const auto& item) -> Eigen::Vector2f {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, ButtonElement>) {
                const auto font = static_cast<float>(item.font_size);
                return {std::max(text_width(item.text, item.font_size) + 12.0f, 24.0f), std::max(font * kLabelHeight + 6.0f, 20.0f)};
            } else if constexpr (std::is_same_v<Item, LabelElement>) {
                std::string sample = "100";
                if (label_text) {
                    sample = label_text(item.content);
                } else if (const auto* fixed = std::get_if<StaticLabelContent>(&item.content)) {
                    sample = fixed->text;
                } else if (const auto* clock = std::get_if<ClockLabelContent>(&item.content)) {
                    sample = Labels::FormatTime(clock->format, nullptr, std::chrono::system_clock::now());
                } else if (std::holds_alternative<TimerLabelContent>(item.content)) {
                    sample = "00:00:00";
                }
                float width = text_width(sample, item.font_size);
                if (const auto* clock = std::get_if<ClockLabelContent>(&item.content); clock != nullptr && clock->city_size) {
                    width = std::max(width, text_width(city_text ? city_text(*clock) : std::string("City"), *clock->city_size));
                }
                return {std::max(width, kMinSize), static_cast<float>(item.font_size) * kLabelHeight + CityAbove(item)};
            } else if constexpr (std::is_same_v<Item, BatteryListElement>) {
                const auto count = static_cast<float>(std::max<size_t>(item.num_devices, 1));
                const float cell = text_width("R99", item.font_size) + 4.0f;
                const float line = static_cast<float>(item.font_size) * kLabelHeight + 4.0f;
                return item.layout == ListLayout::Horizontal ? Eigen::Vector2f(cell * count, line)
                                                             : Eigen::Vector2f(cell, line * count);
            } else if constexpr (std::is_same_v<Item, OverlayListElement>) {
                return {60.0f, static_cast<float>(item.font_size) * kLabelHeight + 8.0f};
            } else {
                return {kMinSize, kMinSize};
            }
        },
        element
    );
}

auto FormDesigner::MinimumToolbarHeight(const ModularUiConfig& config) const -> float {
    float height = kMinToolbarHeight;
    if (!config.Form().toolbar) {
        return height;
    }
    const float padding = config.Form().toolbar->padding;
    for (const auto& element : config.Elements()) {
        if (ModularElements::RegionOf(element) == Region::Toolbar) {
            const bool list = std::holds_alternative<BatteryListElement>(element)
                           || std::holds_alternative<OverlayListElement>(element);
            height = std::max(height, MinimumSize(element).y() + (list ? 0.0f : 2.0f * padding));
        }
    }
    return height;
}

auto FormDesigner::LayerOf(const ModularElement& element) -> int {
    if (std::holds_alternative<PanelElement>(element) || std::holds_alternative<SpriteElement>(element)) {
        return 0;
    }
    if (std::holds_alternative<ButtonElement>(element) || std::holds_alternative<OverlayListElement>(element)) {
        return 2;
    }
    return 1;
}

auto FormDesigner::ToolbarOrder(std::optional<size_t> without) const -> std::vector<size_t> {
    std::vector<size_t> order;
    const auto& elements = Document().Elements();
    for (size_t i = 0; i < elements.size(); ++i) {
        if (i != without && ModularElements::RegionOf(elements[i]) == Region::Toolbar) {
            order.push_back(i);
        }
    }
    return order;
}

auto FormDesigner::InsertionIndex(float x, std::optional<size_t> without) const -> size_t {
    const auto arranged = Document().Arrange();
    const auto order = ToolbarOrder(without);
    return static_cast<size_t>(std::ranges::count_if(order, [&](size_t index) -> bool {
        const auto& rect = ModularElements::Rect(arranged[index]);
        return rect[0] + rect[2] / 2.0f < x;
    }));
}

auto FormDesigner::PlaceInToolbar(size_t index, size_t position) -> size_t {
    auto& elements = m_revisions.Live().Elements();
    auto element = std::move(elements.at(index));
    elements.erase(elements.begin() + static_cast<std::ptrdiff_t>(index));
    ModularElements::SetRegion(element, Region::Toolbar);
    std::vector<size_t> order;
    for (size_t i = 0; i < elements.size(); ++i) {
        if (ModularElements::RegionOf(elements[i]) == Region::Toolbar) {
            order.push_back(i);
        }
    }
    size_t target = elements.size();
    if (position < order.size()) {
        target = order[position];
    } else if (!order.empty()) {
        target = order.back() + 1;
    }
    elements.insert(elements.begin() + static_cast<std::ptrdiff_t>(target), std::move(element));
    return target;
}

auto FormDesigner::Duplicate(size_t index) -> void {
    m_revisions.Edit([index](ModularUiConfig& config) -> void {
        auto copy = config.Elements().at(index);
        if (ModularElements::RegionOf(copy) == Region::Form) {
            auto& rect = ModularElements::Rect(copy);
            rect[0] += 12.0f;
            rect[1] += 12.0f;
        }
        config.Elements().insert(config.Elements().begin() + static_cast<std::ptrdiff_t>(index) + 1, std::move(copy));
    });
    m_selected = index + 1;
}

auto FormDesigner::Remove(size_t index) -> void {
    m_revisions.Edit([index](ModularUiConfig& config) -> void {
        config.Elements().erase(config.Elements().begin() + static_cast<std::ptrdiff_t>(index));
    });
    m_selected.reset();
    m_hovered.reset();
}

auto FormDesigner::Restack(size_t index, int direction) -> void {
    const auto count = Document().Elements().size();
    const auto target = static_cast<std::ptrdiff_t>(index) + direction;
    if (target < 0 || static_cast<size_t>(target) >= count) {
        return;
    }
    m_revisions.Edit([index, target](ModularUiConfig& config) -> void {
        std::swap(config.Elements().at(index), config.Elements().at(static_cast<size_t>(target)));
    });
    m_selected = static_cast<size_t>(target);
}

auto FormDesigner::MakePart(FormPart part) -> ModularElement {
    switch (part) {
    case FormPart::Form: return PanelElement{.rect = {0.0f, 0.0f, 200.0f, 120.0f}, .bg_color = "@background"};
    case FormPart::Button:
        return ButtonElement{
            .rect = {0.0f, 0.0f, 64.0f, 32.0f},
            .font_size = 14,
            .fg_color = "@on-accent",
            .bg_color = "@blue",
            .text = "Button",
        };
    case FormPart::Label:
        return LabelElement{
            .rect = {0.0f, 0.0f, 140.0f, 20.0f},
            .font_size = 16,
            .fg_color = "@text",
            .content = StaticLabelContent{.text = "Label"},
        };
    case FormPart::Clock:
        return LabelElement{
            .rect = {0.0f, 0.0f, 150.0f, 40.0f},
            .font_size = 36,
            .fg_color = "@text",
            .content = ClockLabelContent{.format = "%H:%M"},
        };
    case FormPart::Date:
        return LabelElement{
            .rect = {0.0f, 0.0f, 140.0f, 20.0f},
            .font_size = 14,
            .fg_color = "@text",
            .content = ClockLabelContent{.format = "%x"},
        };
    case FormPart::Panel: return PanelElement{.rect = {0.0f, 0.0f, 160.0f, 80.0f}, .bg_color = "@surface"};
    case FormPart::Battery:
        return BatteryListElement{
            .rect = {0.0f, 0.0f, 300.0f, 24.0f},
            .font_size = 14,
            .fg_color = "@teal",
            .fg_color_low = "@red",
            .fg_color_charging = "@blue",
            .low_threshold = 33.0f,
            .num_devices = 4,
        };
    case FormPart::Screens:
        return OverlayListElement{
            .rect = {0.0f, 0.0f, 200.0f, 36.0f},
            .font_size = 14,
            .fg_color = "@text",
            .bg_color = "@panel",
            .overlay_template = {
                .click_up = OverlayToggleVisible{},
                .right_up = OverlayToggleImmovable{},
                .middle_up = OverlayToggleInteraction{},
            },
        };
    }
    return PanelElement{};
}

auto FormDesigner::PartName(FormPart part) -> std::string_view {
    switch (part) {
    case FormPart::Form: return "Form";
    case FormPart::Button: return "Button";
    case FormPart::Label: return "Text";
    case FormPart::Clock: return "Clock";
    case FormPart::Date: return "Date";
    case FormPart::Panel: return "Panel";
    case FormPart::Battery: return "Batteries";
    case FormPart::Screens: return "Screens";
    }
    return {};
}

auto FormDesigner::Draw(Canvas& canvas, std::optional<int> layer) const -> void {
    const auto& config = Document();
    const auto size = PreviewSize();
    const auto origin = Origin();
    const bool back = layer.value_or(0) == 0;
    const bool front = layer.value_or(kLayers - 1) == kLayers - 1;
    const auto in_layer = [&](size_t index) -> bool { return !layer || LayerOf(config.Elements().at(index)) == *layer; };
    const Eigen::Vector2f pad = Eigen::Vector2f::Constant(kPad);
    if (back) {
        canvas.SetBgColor(EditorColors::Hex(layer ? "#11111b80" : "#11111bc0"));
        canvas.Panel(0.0f, 0.0f, size.x(), size.y(), Theme::Radius(g_themes->Active().rounding, Shape::Panel) * kScale);
        Modular::FillArranged(canvas, config.Backgrounds(origin), kScale, pad);
    }
    auto arranged = config.Arrange(origin);
    if (layer) {
        std::vector<ModularElement> shown;
        for (size_t i = 0; i < arranged.size(); ++i) {
            if (in_layer(i)) {
                shown.push_back(std::move(arranged[i]));
            }
        }
        arranged = std::move(shown);
    }
    Modular::FillArranged(canvas, arranged, kScale, pad);

    const auto to_preview = [this](const CanvasRect& rect) -> CanvasRect {
        const Eigen::Vector2f corner = ToPreview({rect.x, rect.y});
        return {corner.x(), corner.y(), rect.w * kScale, rect.h * kScale};
    };
    const auto outline = [&](const CanvasRect& rect, const Eigen::Vector4f& color, float width, float grow) -> void {
        const auto shown = to_preview({rect.x - grow, rect.y - grow, rect.w + 2.0f * grow, rect.h + 2.0f * grow});
        canvas.Decoration(shown.x, shown.y, shown.w, shown.h, 4.0f * kScale, Outline(color, width));
    };
    const auto block = [&](const CanvasRect& rect, const Eigen::Vector4f& color) -> void {
        const auto shown = to_preview(rect);
        canvas.SetBgColor(color);
        canvas.Panel(shown.x, shown.y, shown.w, shown.h, 3.0f);
    };

    const auto dim = WithAlpha(EditorColors::Dim(), 0.45f);
    for (size_t form = 0; form < config.FormCount(); ++form) {
        const bool chosen = m_selected_form == form;
        if (back || chosen) {
            outline(
                Form(form),
                chosen ? EditorColors::Accent() : (back ? dim : WithAlpha(EditorColors::Dim(), 0.2f)),
                chosen ? 2.5f : 1.5f,
                1.0f
            );
        }
        if (back) {
            if (const auto handle = Handle(FormHitKind::FormCorner, form)) {
                block(*handle, chosen ? EditorColors::Accent() : WithAlpha(EditorColors::Dim(), 0.9f));
            }
        }
    }
    if (m_drop_form && *m_drop_form < config.FormCount() && config.FormCount() > 1) {
        outline(Form(*m_drop_form), WithAlpha(EditorColors::Accent(), 0.7f), 2.0f, 3.0f);
    }
    if (back) {
        if (const auto toolbar = Toolbar()) {
            outline(*toolbar, dim, 1.5f, 1.0f);
        }
        if (const auto handle = Handle(FormHitKind::ToolbarCorner, 0)) {
            block(*handle, WithAlpha(EditorColors::Dim(), 0.9f));
        }
    }
    const auto count = config.Elements().size();
    if (m_hovered && *m_hovered < count && m_hovered != m_selected && in_layer(*m_hovered)) {
        outline(VisualRect(*m_hovered), Eigen::Vector4f(1.0f, 1.0f, 1.0f, 0.45f), 1.5f, 2.0f);
    }
    if (m_selected && *m_selected < count && in_layer(*m_selected)) {
        const auto rect = VisualRect(*m_selected);
        if (m_drag && m_drag->engaged && (m_drag->hit.kind == FormHitKind::Element || m_drag->hit.kind == FormHitKind::Resize)) {
            const auto lifted = to_preview({rect.x - 4.0f, rect.y - 4.0f, rect.w + 8.0f, rect.h + 8.0f});
            canvas.Decoration(
                lifted.x,
                lifted.y,
                lifted.w,
                lifted.h,
                6.0f * kScale,
                std::make_unique<OutlineControl>(
                    WithAlpha(EditorColors::Accent(), 0.22f),
                    HighlightOutline{.color = EditorColors::Accent(), .width = 3.5f}
                )
            );
        } else {
            outline(rect, EditorColors::Accent(), 2.5f, 2.0f);
        }
        block(CenteredRect(rect.x + rect.w, rect.y + rect.h, kHandle, kHandle), EditorColors::Accent());
    }
    if (m_drag && m_drag->engaged && m_drag->hit.kind == FormHitKind::FormBody) {
        const auto form = to_preview(Form(m_drag->hit.index));
        canvas.Decoration(
            form.x - 4.0f,
            form.y - 4.0f,
            form.w + 8.0f,
            form.h + 8.0f,
            6.0f * kScale,
            std::make_unique<OutlineControl>(
                WithAlpha(EditorColors::Accent(), 0.15f),
                HighlightOutline{.color = EditorColors::Accent(), .width = 3.5f}
            )
        );
    }

    if (!front) {
        return;
    }
    canvas.SetBgColor(EditorColors::Accent());
    for (const auto& guide : m_guides) {
        const Eigen::Vector2f at = ToPreview({guide.position, guide.position});
        if (guide.vertical) {
            canvas.Panel(at.x() - 0.75f, 0.0f, 1.5f, size.y(), 0.0f);
        } else {
            canvas.Panel(0.0f, at.y() - 0.75f, size.x(), 1.5f, 0.0f);
        }
    }
    const auto toolbar = Toolbar();
    std::optional<float> marker = m_toolbar_marker;
    std::optional<CanvasRect> landing = m_landing;
    if (m_carrying) {
        const auto& [part, point] = *m_carrying;
        if (part != FormPart::Form && toolbar && Contains(*toolbar, point)) {
            const auto placed = config.Arrange(origin);
            const auto order = ToolbarOrder(std::nullopt);
            const auto position = InsertionIndex(point.x(), std::nullopt);
            if (position < order.size()) {
                marker = ModularElements::Rect(placed[order[position]])[0] - 2.0f;
            } else if (order.empty()) {
                marker = toolbar->x + 4.0f;
            } else {
                const auto& last = ModularElements::Rect(placed[order.back()]);
                marker = last[0] + last[2] + 2.0f;
            }
        } else {
            const auto element = MakePart(part);
            const auto& rect = ModularElements::Rect(element);
            const float h = BaselineLabel(element) != nullptr ? static_cast<float>(FontSize(element)) * kLabelHeight : rect[3];
            landing = CenteredRect(point.x(), point.y(), rect[2], h);
        }
    }
    if (marker && toolbar) {
        block({*marker - 1.5f, toolbar->y, 3.0f, toolbar->h}, EditorColors::Accent());
    }
    if (landing) {
        const auto shown = to_preview(*landing);
        canvas.Decoration(
            shown.x,
            shown.y,
            shown.w,
            shown.h,
            4.0f * kScale,
            std::make_unique<OutlineControl>(
                WithAlpha(EditorColors::Accent(), 0.2f),
                HighlightOutline{.color = EditorColors::Accent(), .width = 2.0f}
            )
        );
    }
}
