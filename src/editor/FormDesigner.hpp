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
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <Eigen/Core>

#include "Canvas.hpp"
#include "History.hpp"
#include "Modular.hpp"

class AppState;

enum class FormHitKind : uint8_t {
    None,
    Element,
    Resize,
    FormBody,
    FormCorner,
    ToolbarCorner,
};

struct FormHit {
    FormHitKind kind = FormHitKind::None;
    size_t index = 0;
};

struct Guide {
    bool vertical = true;
    float position = 0.0f;
};

enum class FormPart : uint8_t {
    Form,
    Button,
    Label,
    Clock,
    Date,
    Panel,
    Battery,
    Screens,
};

class FormDesigner final {
  public:
    static constexpr float kPad = 24.0f;
    static constexpr float kScale = 1.5f;
    static constexpr float kSnap = 6.0f;
    static constexpr float kHandle = 12.0f;
    static constexpr float kMinSize = 16.0f;
    static constexpr float kFormGap = 4.0f;
    static constexpr int kLayers = 3;
    static constexpr std::array<FormPart, 8> kParts{
        FormPart::Form,
        FormPart::Button,
        FormPart::Label,
        FormPart::Clock,
        FormPart::Date,
        FormPart::Panel,
        FormPart::Battery,
        FormPart::Screens,
    };

    explicit FormDesigner(ModularUiConfig config);

    History<ModularUiConfig> history;
    std::optional<size_t> selected;
    std::optional<size_t> selected_form;
    std::optional<size_t> hovered;
    std::optional<std::pair<FormPart, Eigen::Vector2f>> carrying;
    bool snap = true;
    std::chrono::milliseconds hold{250};
    std::function<float(std::string_view, int)> measure;
    std::function<std::string(const LabelContent&)> label_text;
    std::function<std::string(const ClockLabelContent&)> city_text;

    [[nodiscard]] auto Document() const -> const ModularUiConfig& { return history.Current(); }
    [[nodiscard]] auto PreviewSize() const -> Eigen::Vector2f;
    [[nodiscard]] auto ToDocument(const Eigen::Vector2f& preview) const -> Eigen::Vector2f;
    [[nodiscard]] auto ToPreview(const Eigen::Vector2f& document) const -> Eigen::Vector2f;
    [[nodiscard]] auto Origin() const -> Eigen::Vector2f;
    [[nodiscard]] auto Form(size_t index) const -> CanvasRect;
    [[nodiscard]] auto Toolbar() const -> std::optional<CanvasRect>;
    [[nodiscard]] auto FormAt(const Eigen::Vector2f& point) const -> std::optional<size_t>;
    [[nodiscard]] auto VisualRect(size_t index) const -> CanvasRect;
    [[nodiscard]] auto HitTest(const Eigen::Vector2f& point, std::optional<int> layer = std::nullopt) const -> FormHit;
    [[nodiscard]] auto Accepts(const Eigen::Vector2f& point, int layer) const -> bool;
    [[nodiscard]] auto Guides() const -> std::span<const Guide> { return guides_; }
    [[nodiscard]] auto MinimumSize(const ModularElement& element) const -> Eigen::Vector2f;
    [[nodiscard]] auto MinimumForm(const ModularUiConfig& config, size_t form = 0) const -> Eigen::Vector2f;
    [[nodiscard]] auto MinimumToolbarHeight(const ModularUiConfig& config) const -> float;
    auto Fit(ModularUiConfig& config, size_t index) const -> void;
    [[nodiscard]] auto Dragging() const -> bool { return drag_.has_value(); }
    [[nodiscard]] auto Engaged() const -> bool { return drag_ && drag_->engaged; }

    auto Hover(const Eigen::Vector2f& point) -> void;
    auto Press(const Eigen::Vector2f& point, std::optional<int> layer = std::nullopt) -> void;
    auto Move(const Eigen::Vector2f& point) -> void;
    auto Release(const Eigen::Vector2f& point) -> bool;
    auto Insert(FormPart part, const std::optional<Eigen::Vector2f>& point) -> std::optional<size_t>;
    auto Duplicate(size_t index) -> void;
    auto Remove(size_t index) -> void;
    auto RemoveForm(size_t form) -> void;
    auto Restack(size_t index, int direction) -> void;
    auto Draw(Canvas& canvas, AppState& app, std::optional<int> layer = std::nullopt) const -> void;

    static auto LayerOf(const ModularElement& element) -> int;
    static auto MakePart(FormPart part) -> ModularElement;
    static auto PartName(FormPart part) -> std::string_view;

  private:
    struct Drag {
        FormHit hit;
        Eigen::Vector2f start = Eigen::Vector2f::Zero();
        ModularUiConfig before;
        std::array<float, 4> rect{};
        Eigen::Vector2f start_position = Eigen::Vector2f::Zero();
        int font_size = 0;
        bool moved = false;
        bool engaged = false;
        bool reselected = false;
        std::chrono::steady_clock::time_point pressed_at;
        size_t form = 0;
    };

    struct SnapLock {
        bool active = false;
        size_t edge = 0;
        float target = 0.0f;
    };

    [[nodiscard]] auto ToolbarOrder(std::optional<size_t> without) const -> std::vector<size_t>;
    [[nodiscard]] auto InsertionIndex(float x, std::optional<size_t> without) const -> size_t;
    [[nodiscard]] auto InToolbar(const Eigen::Vector2f& point) const -> bool;
    [[nodiscard]] auto Handle(FormHitKind kind, size_t form) const -> std::optional<CanvasRect>;
    [[nodiscard]] auto Extent() const -> Eigen::Vector2f;
    auto MoveElement(Drag& drag, const Eigen::Vector2f& point) -> void;
    auto ClampIntoForm(size_t index) -> void;
    auto MoveForm(const Drag& drag, const Eigen::Vector2f& point) -> void;
    auto ResizeElement(const Drag& drag, const Eigen::Vector2f& point) -> void;
    auto ResizeRegion(const Drag& drag, const Eigen::Vector2f& point) -> void;
    auto PlaceInToolbar(size_t index, size_t position) -> size_t;
    auto PlaceInForm(size_t index, const Eigen::Vector2f& center) -> void;
    auto InsertForm(const std::optional<Eigen::Vector2f>& point) -> void;
    auto SnapAxis(float start, float size, std::span<const float> targets, bool vertical, float offset, SnapLock& lock) -> float;

    std::optional<Drag> drag_;
    std::vector<Guide> guides_;
    std::optional<float> toolbar_marker_;
    std::optional<CanvasRect> landing_;
    std::optional<Eigen::Vector2f> frozen_origin_;
    std::optional<size_t> drop_form_;
    SnapLock snap_x_;
    SnapLock snap_y_;
};
