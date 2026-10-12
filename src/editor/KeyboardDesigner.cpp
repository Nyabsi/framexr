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

#include "KeyboardDesigner.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <utility>

#include "EditorColors.hpp"
#include "Keyboard.hpp"
#include "OutlineControl.hpp"
#include "Theme.hpp"

namespace {

constexpr float kPreviewStep = 32.0f;
constexpr float kMoveThreshold = 3.0f;
constexpr float kControlGap = 15.0f;
constexpr float kControlSize = 60.0f;
constexpr float kEpsilon = 0.001f;

constexpr std::string_view kNumpad = R"(
name: numpad
columns: 4
rows: 5
keys:
  - { at: [0, 0], key: NumLock, label: [Num] }
  - { at: [1, 0], key: KP_Divide, label: [" /"] }
  - { at: [2, 0], key: KP_Multiply, label: [" *"] }
  - { at: [3, 0], key: KP_Subtract, label: [" -"] }
  - { at: [0, 1], key: KP_7 }
  - { at: [1, 1], key: KP_8 }
  - { at: [2, 1], key: KP_9 }
  - { at: [3, 1], key: KP_Add, label: [" +"] }
  - { at: [0, 2], key: KP_4 }
  - { at: [1, 2], key: KP_5 }
  - { at: [2, 2], key: KP_6 }
  - { at: [0, 3], key: KP_1 }
  - { at: [1, 3], key: KP_2 }
  - { at: [2, 3], key: KP_3 }
  - { at: [3, 3], key: KP_Enter, label: [Ent] }
  - { at: [0, 4], width: 2, key: KP_0 }
  - { at: [2, 4], key: KP_Decimal, label: [" ."] }
)";

constexpr std::string_view kNavigation = R"(
name: navigation
columns: 3
rows: 2
keys:
  - { at: [0, 0], key: Insert, label: [Ins] }
  - { at: [1, 0], key: Home }
  - { at: [2, 0], key: Prior, label: [PgUp] }
  - { at: [0, 1], key: Delete, label: [Del] }
  - { at: [1, 1], key: End }
  - { at: [2, 1], key: Next, label: [PgDn] }
)";

constexpr std::string_view kArrows = R"(
name: arrows
columns: 3
rows: 2
keys:
  - { at: [1, 0], key: Up }
  - { at: [0, 1], key: Left }
  - { at: [1, 1], key: Down }
  - { at: [2, 1], key: Right }
)";

constexpr std::string_view kFunction = R"(
name: function
columns: 12
rows: 1
keys:
  - { at: [0, 0], key: F1 }
  - { at: [1, 0], key: F2 }
  - { at: [2, 0], key: F3 }
  - { at: [3, 0], key: F4 }
  - { at: [4, 0], key: F5 }
  - { at: [5, 0], key: F6 }
  - { at: [6, 0], key: F7 }
  - { at: [7, 0], key: F8 }
  - { at: [8, 0], key: F9 }
  - { at: [9, 0], key: F10 }
  - { at: [10, 0], key: F11 }
  - { at: [11, 0], key: F12 }
)";

constexpr std::string_view kSingleKey = R"(
name: key
columns: 1
rows: 1
keys:
  - { at: [0, 0], action: Blank }
)";

auto Load(std::string_view text) -> KeyboardLayout {
    auto parsed = KeyboardLayout::Parse(text);
    if (!parsed) {
        FatalError(std::format("Invalid keyboard template: {}", parsed.error().Message()));
    }
    return std::move(*parsed);
}

auto Contains(const CanvasRect& rect, const Eigen::Vector2f& point) -> bool {
    return point.x() >= rect.x && point.x() <= rect.x + rect.w && point.y() >= rect.y && point.y() <= rect.y + rect.h;
}

auto Scale(const CanvasRect& rect, float scale) -> CanvasRect {
    return {rect.x * scale, rect.y * scale, rect.w * scale, rect.h * scale};
}

auto WithAlpha(Eigen::Vector4f color, float alpha) -> Eigen::Vector4f {
    color.w() = alpha;
    return color;
}

auto IndexOf(const KeyboardLayout& layout, float x, int row) -> std::optional<size_t> {
    for (size_t i = 0; i < layout.Keys().size(); ++i) {
        if (layout.Keys()[i].row == row && std::abs(layout.Keys()[i].x - x) < kEpsilon) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace

KeyboardDesigner::KeyboardDesigner(KeyboardLayout layout) : m_revisions(std::move(layout)) {
}

auto KeyboardDesigner::Template(KeyTemplate key_template) -> const KeyboardLayout& {
    static const std::map<KeyTemplate, KeyboardLayout> templates{
        {KeyTemplate::Key, Load(kSingleKey)},
        {KeyTemplate::Numpad, Load(kNumpad)},
        {KeyTemplate::Navigation, Load(kNavigation)},
        {KeyTemplate::Arrows, Load(kArrows)},
        {KeyTemplate::Function, Load(kFunction)},
    };
    return templates.at(key_template);
}

auto KeyboardDesigner::TemplateName(KeyTemplate key_template) -> std::string_view {
    switch (key_template) {
    case KeyTemplate::Key: return "Key";
    case KeyTemplate::Numpad: return "Numpad";
    case KeyTemplate::Navigation: return "Navigation";
    case KeyTemplate::Arrows: return "Arrows";
    case KeyTemplate::Function: return "F-row";
    }
    return {};
}

auto KeyboardDesigner::PreviewSize() const -> Eigen::Vector2f {
    const Eigen::Vector2f size = (Keyboard::CanvasSize(Document()) + Eigen::Vector2f::Constant(kControls)) * kScale;
    return (size / kPreviewStep).array().ceil().matrix() * kPreviewStep;
}

auto KeyboardDesigner::ToGrid(const Eigen::Vector2f& preview) const -> Eigen::Vector2f {
    return (preview / kScale - Eigen::Vector2f::Constant(Keyboard::kKeyboardMargin)) / Keyboard::kPixelsPerUnit;
}

auto KeyboardDesigner::CellRect(float x, int row, float width) const -> CanvasRect {
    return Keyboard::KeyRect({.x = x, .row = row, .width = width}, {.scale = kScale});
}

auto KeyboardDesigner::KeyPreviewCenter(size_t index) const -> Eigen::Vector2f {
    const auto rect = Keyboard::KeyRect(Document().Keys().at(index), {.scale = kScale});
    return {rect.x + rect.w / 2.0f, rect.y + rect.h / 2.0f};
}

auto KeyboardDesigner::ControlRect(KeyHitKind kind) const -> CanvasRect {
    const auto size = Keyboard::CanvasSize(Document());
    const float margin = Keyboard::kKeyboardMargin;
    const float half_height = (size.y() - 2.0f * margin) / 2.0f;
    const float half_width = (size.x() - 2.0f * margin) / 2.0f;
    CanvasRect rect;
    switch (kind) {
    case KeyHitKind::AddColumn: rect = {size.x() + kControlGap, margin, kControlSize, half_height - 5.0f}; break;
    case KeyHitKind::RemoveColumn:
        rect = {size.x() + kControlGap, margin + half_height + 5.0f, kControlSize, half_height - 5.0f};
        break;
    case KeyHitKind::AddRow: rect = {margin, size.y() + kControlGap, half_width - 5.0f, kControlSize}; break;
    case KeyHitKind::RemoveRow: rect = {margin + half_width + 5.0f, size.y() + kControlGap, half_width - 5.0f, kControlSize}; break;
    case KeyHitKind::None:
    case KeyHitKind::Key:
    case KeyHitKind::Cell: break;
    }
    return Scale(rect, kScale);
}

auto KeyboardDesigner::HitTest(const Eigen::Vector2f& preview) const -> KeyHit {
    for (const auto kind : {KeyHitKind::AddColumn, KeyHitKind::RemoveColumn, KeyHitKind::AddRow, KeyHitKind::RemoveRow}) {
        if (Contains(ControlRect(kind), preview)) {
            return {.kind = kind};
        }
    }
    const auto& layout = Document();
    const Eigen::Vector2f grid = ToGrid(preview);
    const int row = static_cast<int>(std::floor(grid.y()));
    if (row < 0 || row >= layout.Rows() || grid.x() < 0.0f || grid.x() >= layout.Columns()) {
        return {};
    }
    if (const auto key = layout.KeyAt(grid.x(), row)) {
        return {.kind = KeyHitKind::Key, .index = *key, .x = layout.Keys()[*key].x, .row = row};
    }
    const float cell = std::floor(grid.x());
    if (layout.Fits(cell, row, 1.0f)) {
        return {.kind = KeyHitKind::Cell, .x = cell, .row = row};
    }
    return {};
}

auto KeyboardDesigner::DropCell(const Eigen::Vector2f& preview) const -> std::pair<float, int> {
    const Eigen::Vector2f grid = ToGrid(preview);
    return {
        std::max(KeyboardLayout::Snap(std::floor(grid.x() * 4.0f) / 4.0f), 0.0f),
        std::max(static_cast<int>(std::floor(grid.y())), 0)
    };
}

auto KeyboardDesigner::Hover(const Eigen::Vector2f& preview) -> void {
    if (m_drag) {
        Move(preview);
    }
    m_hovered = HitTest(preview);
    if (m_carrying) {
        m_carrying->second = preview;
    }
}

auto KeyboardDesigner::Press(const Eigen::Vector2f& preview) -> void {
    const auto hit = HitTest(preview);
    if (m_armed && hit.kind != *m_armed) {
        m_armed.reset();
    }
    Drag drag{.hit = hit, .start = preview, .engaged = m_hold.count() == 0, .pressed_at = std::chrono::steady_clock::now()};
    if (hit.kind == KeyHitKind::Key) {
        drag.reselected = m_selected == hit.index;
        m_selected = hit.index;
        const auto& key = Document().Keys()[hit.index];
        drag.grab = ToGrid(preview) - Eigen::Vector2f(key.x, static_cast<float>(key.row));
        drag.x = key.x;
        drag.row = key.row;
    } else if (hit.kind == KeyHitKind::None) {
        m_selected.reset();
    }
    m_drag = drag;
}

auto KeyboardDesigner::Move(const Eigen::Vector2f& preview) -> void {
    if (!m_drag || m_drag->hit.kind != KeyHitKind::Key) {
        return;
    }
    if (!m_drag->engaged) {
        if (std::chrono::steady_clock::now() - m_drag->pressed_at < m_hold) {
            return;
        }
        m_drag->engaged = true;
        m_drag->start = preview;
        return;
    }
    if ((preview - m_drag->start).norm() > kMoveThreshold) {
        m_drag->moved = true;
    }
    if (!m_drag->moved) {
        return;
    }
    const auto& layout = Document();
    const auto& key = layout.Keys().at(m_drag->hit.index);
    const Eigen::Vector2f grid = ToGrid(preview) - m_drag->grab;
    m_drag->x = KeyboardLayout::Snap(grid.x());
    m_drag->row = static_cast<int>(std::lround(grid.y()));
    m_drag->fits = layout.Fits(m_drag->x, m_drag->row, key.width, m_drag->hit.index);
}

auto KeyboardDesigner::Release(const Eigen::Vector2f& preview) -> bool {
    if (!m_drag) {
        return false;
    }
    const auto drag = *m_drag;
    m_drag.reset();
    switch (drag.hit.kind) {
    case KeyHitKind::Key: {
        if (!drag.moved && drag.reselected) {
            m_selected.reset();
            return false;
        }
        const auto& key = Document().Keys().at(drag.hit.index);
        if (!drag.engaged || !drag.moved || !drag.fits || (std::abs(key.x - drag.x) < kEpsilon && key.row == drag.row)) {
            return false;
        }
        m_revisions.Edit([&drag](KeyboardLayout& layout) -> void {
            auto& moved = layout.Keys().at(drag.hit.index);
            moved.x = drag.x;
            moved.row = drag.row;
            layout.SortKeys();
        });
        m_selected = IndexOf(Document(), drag.x, drag.row);
        return true;
    }
    case KeyHitKind::None: return false;
    case KeyHitKind::Cell:
    case KeyHitKind::AddColumn:
    case KeyHitKind::RemoveColumn:
    case KeyHitKind::AddRow:
    case KeyHitKind::RemoveRow: break;
    }
    if (HitTest(preview) != drag.hit) {
        return false;
    }
    switch (drag.hit.kind) {
    case KeyHitKind::Cell: AddKey(drag.hit.x, drag.hit.row); return true;
    case KeyHitKind::AddColumn: AddColumn(); return true;
    case KeyHitKind::RemoveColumn: return RemoveColumn();
    case KeyHitKind::AddRow: AddRow(); return true;
    case KeyHitKind::RemoveRow: return RemoveRow();
    case KeyHitKind::None:
    case KeyHitKind::Key: break;
    }
    return false;
}

auto KeyboardDesigner::AddKey(float x, int row) -> void {
    m_revisions.Edit([x, row](KeyboardLayout& layout) -> void {
        layout.Keys().push_back({.x = x, .row = row, .width = 1.0f, .action = BlankKey{}});
        layout.SortKeys();
    });
    m_selected = IndexOf(Document(), x, row);
}

auto KeyboardDesigner::AddColumn() -> void {
    m_armed.reset();
    m_revisions.Edit([](KeyboardLayout& layout) -> void { layout.SetColumns(layout.Columns() + 1.0f); });
}

auto KeyboardDesigner::RemoveColumn() -> bool {
    const auto& layout = Document();
    const float columns = std::max(layout.Columns() - 1.0f, 1.0f);
    if (columns == layout.Columns()) {
        return false;
    }
    const bool loses_keys = std::ranges::any_of(layout.Keys(), [columns](const KeyDefinition& key) -> bool {
        return KeyboardLayout::RightEdge(key) > columns + kEpsilon;
    });
    if (loses_keys && m_armed != KeyHitKind::RemoveColumn) {
        m_armed = KeyHitKind::RemoveColumn;
        return false;
    }
    m_armed.reset();
    m_selected.reset();
    m_revisions.Edit([columns](KeyboardLayout& edited) -> void {
        std::erase_if(edited.Keys(), [columns](const KeyDefinition& key) -> bool {
            return KeyboardLayout::RightEdge(key) > columns + kEpsilon;
        });
        edited.SetColumns(columns);
    });
    return true;
}

auto KeyboardDesigner::AddRow() -> void {
    m_armed.reset();
    m_revisions.Edit([](KeyboardLayout& layout) -> void { layout.SetRows(layout.Rows() + 1); });
}

auto KeyboardDesigner::RemoveRow() -> bool {
    const auto& layout = Document();
    if (layout.Rows() <= 1) {
        return false;
    }
    const int rows = layout.Rows() - 1;
    const bool loses_keys = std::ranges::any_of(layout.Keys(), [rows](const KeyDefinition& key) -> bool {
        return key.row >= rows;
    });
    if (loses_keys && m_armed != KeyHitKind::RemoveRow) {
        m_armed = KeyHitKind::RemoveRow;
        return false;
    }
    m_armed.reset();
    m_selected.reset();
    m_revisions.Edit([rows](KeyboardLayout& edited) -> void {
        std::erase_if(edited.Keys(), [rows](const KeyDefinition& key) -> bool { return key.row >= rows; });
        edited.SetRows(rows);
    });
    return true;
}

auto KeyboardDesigner::Resize(size_t index, float delta) -> bool {
    const auto& key = Document().Keys().at(index);
    const float width = KeyboardLayout::Snap(key.width + delta);
    if (width < kKeyStep - kEpsilon || !Document().Fits(key.x, key.row, width, index)) {
        return false;
    }
    m_revisions.Edit([index, width](KeyboardLayout& layout) -> void { layout.Keys().at(index).width = width; });
    return true;
}

auto KeyboardDesigner::Duplicate(size_t index) -> bool {
    const auto& layout = Document();
    const auto& key = layout.Keys().at(index);
    std::optional<std::pair<float, int>> spot;
    for (int offset = 0; offset < layout.Rows() && !spot; ++offset) {
        const int row = (key.row + offset) % layout.Rows();
        const int first = offset == 0 ? static_cast<int>(std::lround(KeyboardLayout::RightEdge(key) / kKeyStep)) : 0;
        const int last = static_cast<int>(std::lround((layout.Columns() - key.width) / kKeyStep));
        for (int step = first; step <= last; ++step) {
            const float x = static_cast<float>(step) * kKeyStep;
            if (layout.Fits(x, row, key.width)) {
                spot = std::pair{x, row};
                break;
            }
        }
    }
    if (!spot) {
        return false;
    }
    auto copy = key;
    copy.x = spot->first;
    copy.row = spot->second;
    m_revisions.Edit([&copy](KeyboardLayout& edited) -> void {
        edited.Keys().push_back(copy);
        edited.SortKeys();
    });
    m_selected = IndexOf(Document(), copy.x, copy.row);
    return true;
}

auto KeyboardDesigner::Remove(size_t index) -> void {
    m_revisions.Edit([index](KeyboardLayout& layout) -> void {
        layout.Keys().erase(layout.Keys().begin() + static_cast<std::ptrdiff_t>(index));
    });
    m_selected.reset();
}

auto KeyboardDesigner::Place(KeyTemplate key_template, float x, int row) -> bool {
    const auto& shape = Template(key_template);
    auto layout = Document();
    layout.SetColumns(std::max(layout.Columns(), std::ceil(x + shape.Columns())));
    layout.SetRows(std::max(layout.Rows(), row + shape.Rows()));
    for (const auto& key : shape.Keys()) {
        auto placed = key;
        placed.x += x;
        placed.row += row;
        if (!layout.Fits(placed.x, placed.row, placed.width)) {
            return false;
        }
        layout.Keys().push_back(std::move(placed));
    }
    layout.SortKeys();
    m_revisions.Edit([&layout](KeyboardLayout& edited) -> void { edited = std::move(layout); });
    m_selected = shape.Keys().size() == 1 ? IndexOf(Document(), x + shape.Keys().front().x, row + shape.Keys().front().row)
                                          : std::nullopt;
    return true;
}

auto KeyboardDesigner::Draw(Canvas& canvas, const std::shared_ptr<const Keymap>& keymap) const -> void {
    const auto& layout = Document();
    const auto size = PreviewSize();
    canvas.SetBgColor(EditorColors::Hex("#11111bc0"));
    canvas.Panel(0.0f, 0.0f, size.x(), size.y(), Theme::Radius(g_themes->Active().rounding, Shape::Panel) * kScale);
    Keyboard::Fill(canvas, layout, keymap, std::make_shared<KeyboardState>(), {.scale = kScale, .interactive = false});

    const auto outline =
        [&canvas](const CanvasRect& rect, const std::optional<Eigen::Vector4f>& fill, const Eigen::Vector4f& color, float width)
        -> void {
        canvas.Decoration(
            rect.x - 2.0f,
            rect.y - 2.0f,
            rect.w + 4.0f,
            rect.h + 4.0f,
            6.0f,
            std::make_unique<OutlineControl>(fill, HighlightOutline{.color = color, .width = width})
        );
    };
    const auto button =
        [&canvas](const CanvasRect& rect, std::string_view text, const Eigen::Vector4f& bg, const Eigen::Vector4f& fg) -> void {
        canvas.SetBgColor(bg);
        canvas.Panel(rect.x, rect.y, rect.w, rect.h, 6.0f);
        canvas.SetFgColor(fg);
        canvas.SetFontSize(16);
        canvas.LabelCentered(rect.x, rect.y, rect.w, rect.h, 0.0f, std::string(text));
    };

    const auto faint = WithAlpha(EditorColors::Surface(), 0.35f);
    for (const auto& [x, row] : layout.FreeCells()) {
        const bool lit = m_hovered && m_hovered->kind == KeyHitKind::Cell && m_hovered->x == x && m_hovered->row == row;
        button(CellRect(x, row, 1.0f), "+", lit ? EditorColors::Surface() : faint, EditorColors::Dim());
    }
    const auto control = [&](KeyHitKind kind, std::string_view text) -> void {
        const bool armed = m_armed == kind;
        const bool lit = m_hovered && m_hovered->kind == kind;
        button(
            ControlRect(kind),
            armed ? "Sure?" : text,
            armed ? EditorColors::Danger() : (lit ? EditorColors::Accent() : EditorColors::Surface()),
            armed || lit ? EditorColors::OnAccent() : EditorColors::Text()
        );
    };
    control(KeyHitKind::AddColumn, "+");
    control(KeyHitKind::RemoveColumn, "-");
    control(KeyHitKind::AddRow, "+ row");
    control(KeyHitKind::RemoveRow, "- row");

    if (m_hovered && m_hovered->kind == KeyHitKind::Key && m_hovered->index != m_selected
        && m_hovered->index < layout.Keys().size()) {
        outline(
            Keyboard::KeyRect(layout.Keys()[m_hovered->index], {.scale = kScale}),
            std::nullopt,
            Eigen::Vector4f(1.0f, 1.0f, 1.0f, 0.4f),
            1.5f
        );
    }
    if (m_selected && *m_selected < layout.Keys().size()) {
        const auto rect = Keyboard::KeyRect(layout.Keys()[*m_selected], {.scale = kScale});
        if (m_drag && m_drag->engaged && m_drag->hit.kind == KeyHitKind::Key) {
            outline(
                {rect.x - 3.0f, rect.y - 3.0f, rect.w + 6.0f, rect.h + 6.0f},
                WithAlpha(EditorColors::Accent(), 0.22f),
                EditorColors::Accent(),
                3.5f
            );
        } else {
            outline(rect, std::nullopt, EditorColors::Accent(), 2.5f);
        }
    }
    if (m_drag && m_drag->moved && m_drag->hit.kind == KeyHitKind::Key) {
        const auto& key = layout.Keys().at(m_drag->hit.index);
        const auto color = m_drag->fits ? EditorColors::Hex("@green") : EditorColors::Danger();
        outline(CellRect(m_drag->x, m_drag->row, key.width), WithAlpha(color, 0.25f), color, 2.0f);
    }
    if (m_carrying) {
        const auto& [key_template, point] = *m_carrying;
        const auto [x, row] = DropCell(point);
        const auto& shape = Template(key_template);
        auto grown = layout;
        grown.SetColumns(std::max(grown.Columns(), std::ceil(x + shape.Columns())));
        grown.SetRows(std::max(grown.Rows(), row + shape.Rows()));
        for (const auto& key : shape.Keys()) {
            const bool fits = grown.Fits(key.x + x, key.row + row, key.width);
            const auto color = fits ? EditorColors::Hex("@green") : EditorColors::Danger();
            outline(CellRect(key.x + x, key.row + row, key.width), WithAlpha(color, 0.25f), color, 2.0f);
        }
    }
}
