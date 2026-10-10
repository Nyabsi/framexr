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

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "Error.hpp"

namespace YAML {
class Node;
}

class Yaml {
  public:
    static auto Parse(std::string_view text) -> Result<nlohmann::json>;
    static auto Emit(const nlohmann::json& value) -> std::string;

  private:
    static auto Convert(const YAML::Node& node) -> nlohmann::json;
    static auto ConvertScalar(const YAML::Node& node) -> nlohmann::json;
};
