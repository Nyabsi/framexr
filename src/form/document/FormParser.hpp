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
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <Eigen/Core>
#include <nlohmann/json.hpp>

#include "Error.hpp"
#include "FormComponent.hpp"
#include "FormTheme.hpp"
#include "Json.hpp"
#include "document/Common.hpp"
#include "document/FormDocument.hpp"

class FormParser final
{
  public:
    static auto Parse(std::string_view yaml) -> Result<FormDocument>;
    static auto Emit(const FormDocument& document) -> std::string;

    static auto ToRect(const std::array<float, 4>& rect) -> CanvasRect;
    static auto Color(const std::string& color, const ThemeDefinition& theme) -> Eigen::Vector4f;
    static auto Layout(const nlohmann::json& json) -> ListLayout;
    static auto PutLayout(nlohmann::ordered_json& json, ListLayout layout) -> void;
    static auto Timezone(const nlohmann::json& json) -> std::optional<std::variant<size_t, std::string>>;
    static auto PutTimezone(nlohmann::ordered_json& json, const std::variant<size_t, std::string>& zone) -> void;
    static auto PutRadius(nlohmann::ordered_json& json, std::optional<float> radius) -> void;
    static auto Merge(nlohmann::ordered_json& json, const nlohmann::ordered_json& extra) -> void;

    template <typename Enum, size_t Count>
    static auto ParseName(
        const nlohmann::json& json,
        const std::array<std::pair<Enum, std::string_view>, Count>& names,
        std::string_view type
    ) -> Enum
    {
        const auto name = Json::VariantName(json);
        for (const auto& [value, value_name] : names)
        {
            if (value_name == name)
            {
                return value;
            }
        }
        throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for {}", name, type), &json);
    }

    template <typename Enum, size_t Count>
    static auto NameOf(Enum value, const std::array<std::pair<Enum, std::string_view>, Count>& names) -> std::string_view
    {
        for (const auto& [item, name] : names)
        {
            if (item == value)
            {
                return name;
            }
        }
        return names.front().second;
    }

    template <typename Value>
    static auto Optional(const nlohmann::json& json, std::string_view key) -> std::optional<Value>
    {
        const auto it = json.find(key);
        if (it == json.end() || it->is_null())
        {
            return std::nullopt;
        }
        return it->get<Value>();
    }
};
