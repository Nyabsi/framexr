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

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <Eigen/Core>

#include "FormTheme.hpp"
#include "Image.hpp"
#include "methods/LaserPointer.hpp"

struct CanvasRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct HighlightOutline {
    Eigen::Vector4f color = Eigen::Vector4f::Zero();
    float width = 0.0f;
};

class FormComponent;

struct FillItem {
    CanvasRect rect;
    float radius = 0.0f;
    Eigen::Vector4f color = Eigen::Vector4f::Zero();
    FormComponent* owner = nullptr;
};

struct SpriteItem {
    CanvasRect rect;
    std::shared_ptr<Image> image;
    Eigen::Vector4f st{1.0f, 1.0f, 0.0f, 0.0f};
    Eigen::Vector4f tint = Eigen::Vector4f::Ones();
    FormComponent* owner = nullptr;
};

struct TextItem {
    CanvasRect rect;
    std::string text;
    int font_size = 0;
    Eigen::Vector4f color = Eigen::Vector4f::Ones();
    bool centered = false;
    FormComponent* owner = nullptr;
};

struct InteractItem {
    CanvasRect rect;
    float radius = 0.0f;
    FormComponent* owner = nullptr;
    bool clickable = true;
};

using FormItem = std::variant<FillItem, SpriteItem, TextItem, InteractItem>;

using FormState = std::vector<FormItem>;

struct FormFrame {
    CanvasRect rect;
    float scale = 1.0f;
    Rounding rounding = Rounding::Round;
};

using PressSignal = std::function<void(PointerMode)>;

using ReleaseSignal = std::function<void()>;

using ScrollSignal = std::function<void(float, float)>;

using HighlightSignal = std::function<std::optional<Eigen::Vector4f>()>;

using OutlineSignal = std::function<std::optional<HighlightOutline>()>;

class FormComponent {
  public:
    explicit FormComponent(std::string name = {});
    virtual ~FormComponent() = default;
    FormComponent(const FormComponent&) = delete;
    auto operator=(const FormComponent&) -> FormComponent& = delete;

    [[nodiscard]] auto Name() const -> const std::string& { return m_name; }
    [[nodiscard]] auto Parent() const -> FormComponent* { return m_parent; }
    [[nodiscard]] auto Position() const -> const Eigen::Vector2f& { return m_position; }
    [[nodiscard]] auto Size() const -> const Eigen::Vector2f& { return m_size; }
    [[nodiscard]] auto Rect() const -> CanvasRect { return {m_position.x(), m_position.y(), m_size.x(), m_size.y()}; }
    [[nodiscard]] auto Scale() const -> float { return m_scale; }
    [[nodiscard]] auto Z() const -> int { return m_z; }
    [[nodiscard]] auto Dirty() const -> bool { return m_dirty; }
    [[nodiscard]] auto Children() const -> const std::vector<std::unique_ptr<FormComponent>>& { return m_children; }

    auto SetName(std::string name) -> void { m_name = std::move(name); }
    auto SetPosition(Eigen::Vector2f position) -> void { m_position = std::move(position); }
    auto SetSize(Eigen::Vector2f size) -> void { m_size = std::move(size); }
    auto SetRect(const CanvasRect& rect) -> void;
    auto SetScale(float scale) -> void { m_scale = scale; }
    auto SetZ(int z) -> void { m_z = z; }
    auto MarkDirty() -> void { m_dirty = true; }

    auto SetOnPress(PressSignal on_press) -> void { m_on_press = std::move(on_press); }
    auto SetOnRelease(ReleaseSignal on_release) -> void { m_on_release = std::move(on_release); }
    auto SetOnScroll(ScrollSignal on_scroll) -> void { m_on_scroll = std::move(on_scroll); }
    auto SetHighlightColor(HighlightSignal highlight_color) -> void { m_highlight_color = std::move(highlight_color); }
    auto SetOutline(OutlineSignal outline) -> void { m_outline = std::move(outline); }

    template <typename Component, typename... Args>
    auto AddComponent(Args&&... args) -> Component& {
        return AddComponent(std::make_unique<Component>(std::forward<Args>(args)...));
    }

    template <typename Component>
    auto AddComponent(std::unique_ptr<Component> child) -> Component& {
        Component& added = *child;
        Attach(std::move(child));
        return added;
    }

    [[nodiscard]] auto Find(std::string_view name) -> FormComponent*;
    auto Clear() -> void;
    auto Build(FormState& state, const Eigen::Vector2f& origin, float scale, Rounding rounding) -> void;
    auto UpdateAll() -> bool;

    auto OnPress(PointerMode mode) -> void;
    auto OnRelease() -> void;
    auto OnScroll(float delta_y, float delta_x) -> void;
    [[nodiscard]] auto HighlightColor() const -> std::optional<Eigen::Vector4f>;
    [[nodiscard]] auto Outline() const -> std::optional<HighlightOutline>;

    virtual auto Update() -> void;

  protected:
    virtual auto Layout() -> void;
    virtual auto Emit(FormState& state, const FormFrame& frame) -> void;

  private:
    auto Attach(std::unique_ptr<FormComponent> child) -> void;

    std::string m_name;
    FormComponent* m_parent = nullptr;
    std::vector<std::unique_ptr<FormComponent>> m_children;
    Eigen::Vector2f m_position = Eigen::Vector2f::Zero();
    Eigen::Vector2f m_size = Eigen::Vector2f::Zero();
    float m_scale = 1.0f;
    int m_z = 0;
    bool m_dirty = true;
    PressSignal m_on_press;
    ReleaseSignal m_on_release;
    ScrollSignal m_on_scroll;
    HighlightSignal m_highlight_color;
    OutlineSignal m_outline;
};
