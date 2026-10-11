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

#include "Keyboard.hpp"
#include "Logger.hpp"
#include "PanelBuilder.hpp"
#include "State.hpp"
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
    for (size_t i = 0; i < layout.keys.size(); ++i) {
        if (layout.keys[i].row == row && std::abs(layout.keys[i].x - x) < kEpsilon) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace

KeyboardDesigner::KeyboardDesigner(KeyboardLayout layout) : history(std::move(layout)) {
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
    const auto rect = Keyboard::KeyRect(Document().keys.at(index), {.scale = kScale});
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
    if (row < 0 || row >= layout.rows || grid.x() < 0.0f || grid.x() >= layout.columns) {
        return {};
    }
    if (const auto key = layout.KeyAt(grid.x(), row)) {
        return {.kind = KeyHitKind::Key, .index = *key, .x = layout.keys[*key].x, .row = row};
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
    if (drag_) {
        Move(preview);
    }
    hovered = HitTest(preview);
    if (carrying) {
        carrying->second = preview;
    }
}

auto KeyboardDesigner::Press(const Eigen::Vector2f& preview) -> void {
    const auto hit = HitTest(preview);
    if (armed_ && hit.kind != *armed_) {
        armed_.reset();
    }
    Drag drag{.hit = hit, .start = preview, .engaged = hold.count() == 0, .pressed_at = std::chrono::steady_clock::now()};
    if (hit.kind == KeyHitKind::Key) {
        selected = hit.index;
        const auto& key = Document().keys[hit.index];
        drag.grab = ToGrid(preview) - Eigen::Vector2f(key.x, static_cast<float>(key.row));
        drag.x = key.x;
        drag.row = key.row;
    } else if (hit.kind == KeyHitKind::None) {
        selected.reset();
    }
    drag_ = drag;
}

auto KeyboardDesigner::Move(const Eigen::Vector2f& preview) -> void {
    if (!drag_ || drag_->hit.kind != KeyHitKind::Key) {
        return;
    }
    if (!drag_->engaged) {
        if (std::chrono::steady_clock::now() - drag_->pressed_at < hold) {
            return;
        }
        drag_->engaged = true;
        drag_->start = preview;
        return;
    }
    if ((preview - drag_->start).norm() > kMoveThreshold) {
        drag_->moved = true;
    }
    if (!drag_->moved) {
        return;
    }
    const auto& layout = Document();
    const auto& key = layout.keys.at(drag_->hit.index);
    const Eigen::Vector2f grid = ToGrid(preview) - drag_->grab;
    drag_->x = KeyboardLayout::Snap(grid.x());
    drag_->row = static_cast<int>(std::lround(grid.y()));
    drag_->fits = layout.Fits(drag_->x, drag_->row, key.width, drag_->hit.index);
}

auto KeyboardDesigner::Release(const Eigen::Vector2f& preview) -> bool {
    if (!drag_) {
        return false;
    }
    const auto drag = *drag_;
    drag_.reset();
    switch (drag.hit.kind) {
    case KeyHitKind::Key: {
        const auto& key = Document().keys.at(drag.hit.index);
        if (!drag.engaged || !drag.moved || !drag.fits || (std::abs(key.x - drag.x) < kEpsilon && key.row == drag.row)) {
            return false;
        }
        history.Edit([&drag](KeyboardLayout& layout) -> void {
            auto& moved = layout.keys.at(drag.hit.index);
            moved.x = drag.x;
            moved.row = drag.row;
            layout.SortKeys();
        });
        selected = IndexOf(Document(), drag.x, drag.row);
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
    history.Edit([x, row](KeyboardLayout& layout) -> void {
        layout.keys.push_back({.x = x, .row = row, .width = 1.0f, .action = BlankKey{}});
        layout.SortKeys();
    });
    selected = IndexOf(Document(), x, row);
}

auto KeyboardDesigner::AddColumn() -> void {
    armed_.reset();
    history.Edit([](KeyboardLayout& layout) -> void { layout.columns += 1.0f; });
}

auto KeyboardDesigner::RemoveColumn() -> bool {
    const auto& layout = Document();
    const float columns = std::max(layout.columns - 1.0f, 1.0f);
    if (columns == layout.columns) {
        return false;
    }
    const bool loses_keys = std::ranges::any_of(layout.keys, [columns](const KeyDefinition& key) -> bool {
        return key.Right() > columns + kEpsilon;
    });
    if (loses_keys && armed_ != KeyHitKind::RemoveColumn) {
        armed_ = KeyHitKind::RemoveColumn;
        return false;
    }
    armed_.reset();
    selected.reset();
    history.Edit([columns](KeyboardLayout& edited) -> void {
        std::erase_if(edited.keys, [columns](const KeyDefinition& key) -> bool { return key.Right() > columns + kEpsilon; });
        edited.columns = columns;
    });
    return true;
}

auto KeyboardDesigner::AddRow() -> void {
    armed_.reset();
    history.Edit([](KeyboardLayout& layout) -> void { layout.rows += 1; });
}

auto KeyboardDesigner::RemoveRow() -> bool {
    const auto& layout = Document();
    if (layout.rows <= 1) {
        return false;
    }
    const int rows = layout.rows - 1;
    const bool loses_keys = std::ranges::any_of(layout.keys, [rows](const KeyDefinition& key) -> bool { return key.row >= rows; });
    if (loses_keys && armed_ != KeyHitKind::RemoveRow) {
        armed_ = KeyHitKind::RemoveRow;
        return false;
    }
    armed_.reset();
    selected.reset();
    history.Edit([rows](KeyboardLayout& edited) -> void {
        std::erase_if(edited.keys, [rows](const KeyDefinition& key) -> bool { return key.row >= rows; });
        edited.rows = rows;
    });
    return true;
}

auto KeyboardDesigner::Resize(size_t index, float delta) -> bool {
    const auto& key = Document().keys.at(index);
    const float width = KeyboardLayout::Snap(key.width + delta);
    if (width < kKeyStep - kEpsilon || !Document().Fits(key.x, key.row, width, index)) {
        return false;
    }
    history.Edit([index, width](KeyboardLayout& layout) -> void { layout.keys.at(index).width = width; });
    return true;
}

auto KeyboardDesigner::Duplicate(size_t index) -> bool {
    const auto& layout = Document();
    const auto& key = layout.keys.at(index);
    std::optional<std::pair<float, int>> spot;
    for (int offset = 0; offset < layout.rows && !spot; ++offset) {
        const int row = (key.row + offset) % layout.rows;
        const int first = offset == 0 ? static_cast<int>(std::lround(key.Right() / kKeyStep)) : 0;
        const int last = static_cast<int>(std::lround((layout.columns - key.width) / kKeyStep));
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
    history.Edit([&copy](KeyboardLayout& edited) -> void {
        edited.keys.push_back(copy);
        edited.SortKeys();
    });
    selected = IndexOf(Document(), copy.x, copy.row);
    return true;
}

auto KeyboardDesigner::Remove(size_t index) -> void {
    history.Edit([index](KeyboardLayout& layout) -> void {
        layout.keys.erase(layout.keys.begin() + static_cast<std::ptrdiff_t>(index));
    });
    selected.reset();
}

auto KeyboardDesigner::Place(KeyTemplate key_template, float x, int row) -> bool {
    const auto& shape = Template(key_template);
    auto layout = Document();
    layout.columns = std::max(layout.columns, std::ceil(x + shape.columns));
    layout.rows = std::max(layout.rows, row + shape.rows);
    for (const auto& key : shape.keys) {
        auto placed = key;
        placed.x += x;
        placed.row += row;
        if (!layout.Fits(placed.x, placed.row, placed.width)) {
            return false;
        }
        layout.keys.push_back(std::move(placed));
    }
    layout.SortKeys();
    history.Edit([&layout](KeyboardLayout& edited) -> void { edited = std::move(layout); });
    selected = shape.keys.size() == 1 ? IndexOf(Document(), x + shape.keys.front().x, row + shape.keys.front().row) : std::nullopt;
    return true;
}

auto KeyboardDesigner::Draw(Canvas& canvas, AppState& app, const std::shared_ptr<const Keymap>& keymap) const -> void {
    const auto& layout = Document();
    const auto size = PreviewSize();
    canvas.bg_color = EditorColors::Hex("#11111bc0");
    canvas.Panel(0.0f, 0.0f, size.x(), size.y(), Theme::Radius(Theme::Active().rounding, Shape::Panel) * kScale);
    Keyboard::Fill(canvas, layout, app, keymap, std::make_shared<KeyboardState>(), {.scale = kScale, .interactive = false});

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
        canvas.bg_color = bg;
        canvas.Panel(rect.x, rect.y, rect.w, rect.h, 6.0f);
        canvas.fg_color = fg;
        canvas.font_size = 16;
        canvas.LabelCentered(rect.x, rect.y, rect.w, rect.h, 0.0f, std::string(text));
    };

    const auto faint = WithAlpha(EditorColors::Surface(), 0.35f);
    for (const auto& [x, row] : layout.FreeCells()) {
        const bool lit = hovered && hovered->kind == KeyHitKind::Cell && hovered->x == x && hovered->row == row;
        button(CellRect(x, row, 1.0f), "+", lit ? EditorColors::Surface() : faint, EditorColors::Dim());
    }
    const auto control = [&](KeyHitKind kind, std::string_view text) -> void {
        const bool armed = armed_ == kind;
        const bool lit = hovered && hovered->kind == kind;
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

    if (hovered && hovered->kind == KeyHitKind::Key && hovered->index != selected && hovered->index < layout.keys.size()) {
        outline(
            Keyboard::KeyRect(layout.keys[hovered->index], {.scale = kScale}),
            std::nullopt,
            Eigen::Vector4f(1.0f, 1.0f, 1.0f, 0.4f),
            1.5f
        );
    }
    if (selected && *selected < layout.keys.size()) {
        const auto rect = Keyboard::KeyRect(layout.keys[*selected], {.scale = kScale});
        if (drag_ && drag_->engaged && drag_->hit.kind == KeyHitKind::Key) {
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
    if (drag_ && drag_->moved && drag_->hit.kind == KeyHitKind::Key) {
        const auto& key = layout.keys.at(drag_->hit.index);
        const auto color = drag_->fits ? EditorColors::Hex("@green") : EditorColors::Danger();
        outline(CellRect(drag_->x, drag_->row, key.width), WithAlpha(color, 0.25f), color, 2.0f);
    }
    if (carrying) {
        const auto& [key_template, point] = *carrying;
        const auto [x, row] = DropCell(point);
        const auto& shape = Template(key_template);
        auto grown = layout;
        grown.columns = std::max(grown.columns, std::ceil(x + shape.columns));
        grown.rows = std::max(grown.rows, row + shape.rows);
        for (const auto& key : shape.keys) {
            const bool fits = grown.Fits(key.x + x, key.row + row, key.width);
            const auto color = fits ? EditorColors::Hex("@green") : EditorColors::Danger();
            outline(CellRect(key.x + x, key.row + row, key.width), WithAlpha(color, 0.25f), color, 2.0f);
        }
    }
}
