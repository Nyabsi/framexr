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

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include <Eigen/Core>

#include "Form.hpp"
#include "History.hpp"
#include "KeyboardLayout.hpp"

class Keymap;

enum class KeyHitKind : uint8_t {
    None,
    Key,
    Cell,
    AddColumn,
    RemoveColumn,
    AddRow,
    RemoveRow,
};

struct KeyHit {
    KeyHitKind kind = KeyHitKind::None;
    size_t index = 0;
    float x = 0.0f;
    int row = 0;

    auto operator==(const KeyHit&) const -> bool = default;
};

enum class KeyTemplate : uint8_t {
    Key,
    Numpad,
    Navigation,
    Arrows,
    Function,
};

class KeyboardDesigner final {
  public:
    static constexpr float kScale = 0.6f;
    static constexpr float kControls = 90.0f;
    static constexpr std::array<KeyTemplate, 5> kTemplates{
        KeyTemplate::Key,
        KeyTemplate::Numpad,
        KeyTemplate::Navigation,
        KeyTemplate::Arrows,
        KeyTemplate::Function,
    };

    explicit KeyboardDesigner(KeyboardLayout layout);

    [[nodiscard]] auto Revisions() const -> const History<KeyboardLayout>& { return m_revisions; }
    [[nodiscard]] auto Revisions() -> History<KeyboardLayout>& { return m_revisions; }
    [[nodiscard]] auto Selected() const -> const std::optional<size_t>& { return m_selected; }
    [[nodiscard]] auto Selected() -> std::optional<size_t>& { return m_selected; }
    [[nodiscard]] auto Hovered() const -> const std::optional<KeyHit>& { return m_hovered; }
    [[nodiscard]] auto Hovered() -> std::optional<KeyHit>& { return m_hovered; }
    [[nodiscard]] auto Carrying() const -> const std::optional<std::pair<KeyTemplate, Eigen::Vector2f>>& { return m_carrying; }
    [[nodiscard]] auto Carrying() -> std::optional<std::pair<KeyTemplate, Eigen::Vector2f>>& { return m_carrying; }

    auto SetSelected(std::optional<size_t> selected) -> void { m_selected = selected; }
    auto SetCarrying(std::optional<std::pair<KeyTemplate, Eigen::Vector2f>> carrying) -> void { m_carrying = std::move(carrying); }
    auto SetHold(std::chrono::milliseconds hold) -> void { m_hold = hold; }

    [[nodiscard]] auto Document() const -> const KeyboardLayout& { return Revisions().Current(); }
    [[nodiscard]] auto PreviewSize() const -> Eigen::Vector2f;
    [[nodiscard]] auto ToGrid(const Eigen::Vector2f& preview) const -> Eigen::Vector2f;
    [[nodiscard]] auto KeyPreviewCenter(size_t index) const -> Eigen::Vector2f;
    [[nodiscard]] auto HitTest(const Eigen::Vector2f& preview) const -> KeyHit;
    [[nodiscard]] auto RemoveArmed(KeyHitKind kind) const -> bool { return m_armed == kind; }
    [[nodiscard]] auto DropCell(const Eigen::Vector2f& preview) const -> std::pair<float, int>;
    [[nodiscard]] auto Engaged() const -> bool { return m_drag && m_drag->engaged; }

    auto Hover(const Eigen::Vector2f& preview) -> void;
    auto Press(const Eigen::Vector2f& preview) -> void;
    auto Release(const Eigen::Vector2f& preview) -> bool;
    auto AddKey(float x, int row) -> void;
    auto AddColumn() -> void;
    auto RemoveColumn() -> bool;
    auto AddRow() -> void;
    auto RemoveRow() -> bool;
    auto Resize(size_t index, float delta) -> bool;
    auto Duplicate(size_t index) -> bool;
    auto Remove(size_t index) -> void;
    auto Place(KeyTemplate key_template, float x, int row) -> bool;
    auto Draw(Form& form, const std::shared_ptr<const Keymap>& keymap) const -> void;

    static auto Template(KeyTemplate key_template) -> const KeyboardLayout&;
    static auto TemplateName(KeyTemplate key_template) -> std::string_view;

  private:
    struct Drag {
        KeyHit hit;
        Eigen::Vector2f start = Eigen::Vector2f::Zero();
        Eigen::Vector2f grab = Eigen::Vector2f::Zero();
        bool moved = false;
        float x = 0.0f;
        int row = 0;
        bool fits = false;
        bool engaged = false;
        bool reselected = false;
        std::chrono::steady_clock::time_point pressed_at;
    };

    [[nodiscard]] auto ControlRect(KeyHitKind kind) const -> CanvasRect;
    [[nodiscard]] auto CellRect(float x, int row, float width) const -> CanvasRect;
    auto Move(const Eigen::Vector2f& preview) -> void;

    std::optional<Drag> m_drag;
    std::optional<KeyHitKind> m_armed;

    History<KeyboardLayout> m_revisions;
    std::optional<size_t> m_selected;
    std::optional<KeyHit> m_hovered;
    std::optional<std::pair<KeyTemplate, Eigen::Vector2f>> m_carrying;
    std::chrono::milliseconds m_hold{250};
};
