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

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Core>
#include <nlohmann/json.hpp>

#include "FormComponent.hpp"
#include "document/Common.hpp"
#include "document/FormElements.hpp"

enum class ToolbarPosition : uint8_t {
    Above,
    Below,
};

struct ToolbarLayout {
    float height = 40.0f;
    float gap = 0.0f;
    float padding = 2.0f;
    ToolbarPosition position = ToolbarPosition::Below;
};

struct FormLayout {
    float width = 400.0f;
    float height = 160.0f;
    std::optional<ToolbarLayout> toolbar;
    std::optional<std::string> bg_color;
};

struct ExtraForm {
    float x = 0.0f;
    float y = 0.0f;
    float width = 200.0f;
    float height = 120.0f;
    std::optional<std::string> bg_color;
};

class FormDocument {
  public:
    [[nodiscard]] auto Width() const -> float { return m_width; }
    [[nodiscard]] auto Theme() const -> const std::optional<std::string>& { return m_theme; }
    [[nodiscard]] auto SpawnPos() const -> const std::optional<std::array<float, 3>>& { return m_spawn_pos; }
    [[nodiscard]] auto SpawnPos() -> std::optional<std::array<float, 3>>& { return m_spawn_pos; }
    [[nodiscard]] auto Form() const -> const FormLayout& { return m_form; }
    [[nodiscard]] auto Form() -> FormLayout& { return m_form; }
    [[nodiscard]] auto Forms() const -> const std::vector<ExtraForm>& { return m_forms; }
    [[nodiscard]] auto Forms() -> std::vector<ExtraForm>& { return m_forms; }
    [[nodiscard]] auto Components() const -> const std::vector<FormElement>& { return m_components; }
    [[nodiscard]] auto Components() -> std::vector<FormElement>& { return m_components; }

    auto SetWidth(float width) -> void { m_width = width; }
    auto SetTheme(std::optional<std::string> theme) -> void { m_theme = std::move(theme); }
    auto SetForm(FormLayout form) -> void { m_form = std::move(form); }
    auto SetComponents(std::vector<FormElement> components) -> void { m_components = std::move(components); }

    [[nodiscard]] auto Size() const -> std::array<uint32_t, 2>;
    [[nodiscard]] auto Origin() const -> Eigen::Vector2f;
    [[nodiscard]] auto FormCount() const -> size_t { return Forms().size() + 1; }
    [[nodiscard]] auto FormRect(size_t index = 0) const -> CanvasRect;
    [[nodiscard]] auto FormRect(size_t index, const Eigen::Vector2f& origin) const -> CanvasRect;
    [[nodiscard]] auto ToolbarRect() const -> std::optional<CanvasRect>;
    [[nodiscard]] auto ToolbarRect(const Eigen::Vector2f& origin) const -> std::optional<CanvasRect>;
    [[nodiscard]] auto Arrange() const -> std::vector<FormElement>;
    [[nodiscard]] auto Arrange(const Eigen::Vector2f& origin) const -> std::vector<FormElement>;
    [[nodiscard]] auto Backgrounds(const Eigen::Vector2f& origin) const -> std::vector<FormElement>;
    auto RemoveForm(size_t index) -> void;

  private:
    float m_width = 0.0f;
    std::optional<std::string> m_theme;
    std::optional<std::array<float, 3>> m_spawn_pos;
    FormLayout m_form;
    std::vector<ExtraForm> m_forms;
    std::vector<FormElement> m_components;
};

auto from_json(const nlohmann::json& json, FormDocument& value) -> void;

auto to_json(nlohmann::ordered_json& json, const FormDocument& value) -> void;
