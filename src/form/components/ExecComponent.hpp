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
#include <cstddef>
#include <string>
#include <string_view>
#include <chrono>
#include <optional>
#include <vector>

#include "Process.hpp"

#include <nlohmann/json.hpp>

#include "FormComponent.hpp"
#include "LabelComponent.hpp"
#include "document/Common.hpp"

struct ExecElement {
    std::array<float, 4> rect{};
    int font_size = 0;
    std::string fg_color;
    bool centered = false;
    std::vector<std::string> command;
    float interval = 0.0f;
    Region region = Region::Form;
    size_t form = 0;
};

class ExecComponent final : public FormComponent {
  public:
    static constexpr std::string_view kType = "Exec";

    static auto Parse(const nlohmann::json& json) -> ExecElement;
    static auto Serialize(nlohmann::ordered_json& json, const ExecElement& element) -> void;
    static auto Create(FormComponent& parent, const ExecElement& element, const FormContext& context) -> ExecComponent&;

    ExecComponent(std::string name, std::vector<std::string> command, float interval);

    [[nodiscard]] auto Label() -> LabelComponent& { return m_label; }

    auto Update() -> void override;

  protected:
    auto Layout() -> void override;

  private:
    LabelComponent& m_label;
    std::vector<std::string> m_command;
    float m_interval;
    std::chrono::steady_clock::time_point m_last_exec;
    std::optional<Process> m_child;
};
