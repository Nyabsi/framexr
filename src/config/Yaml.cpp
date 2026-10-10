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

#include "Yaml.hpp"

#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>

#include <yaml-cpp/yaml.h>

namespace {

auto IsNullLiteral(const std::string& text) -> bool {
    return text.empty() || text == "~" || text == "null" || text == "Null" || text == "NULL";
}

auto ParseBoolLiteral(const std::string& text) -> std::optional<bool> {
    if (text == "true" || text == "True" || text == "TRUE") {
        return true;
    }
    if (text == "false" || text == "False" || text == "FALSE") {
        return false;
    }
    return std::nullopt;
}

auto ParseIntegerLiteral(std::string_view text) -> std::optional<int64_t> {
    const bool negative = text.starts_with('-');
    if (negative || text.starts_with('+')) {
        text.remove_prefix(1);
    }
    int base = 10;
    if (text.starts_with("0x")) {
        base = 16;
    } else if (text.starts_with("0o")) {
        base = 8;
    } else if (text.starts_with("0b")) {
        base = 2;
    }
    if (base != 10) {
        text.remove_prefix(2);
    }
    if (text.empty()) {
        return std::nullopt;
    }
    int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return negative ? -value : value;
}

auto ParseFloatLiteral(std::string_view text) -> std::optional<double> {
    const bool negative = text.starts_with('-');
    if (negative || text.starts_with('+')) {
        text.remove_prefix(1);
    }
    if (text == ".inf" || text == ".Inf" || text == ".INF") {
        return negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    }
    if (text == ".nan" || text == ".NaN" || text == ".NAN") {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const bool numeric = !text.empty() && text.find_first_not_of("0123456789.eE+-") == std::string_view::npos
                      && text.find_first_of("0123456789") != std::string_view::npos;
    if (!numeric) {
        return std::nullopt;
    }
    double value = 0.0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return negative ? -value : value;
}

} // namespace

auto Yaml::Parse(std::string_view text) -> Result<nlohmann::json> {
    try {
        for (const auto& document : YAML::LoadAll(std::string(text))) {
            if (document.IsDefined() && !document.IsNull()) {
                return Convert(document);
            }
        }
        return nlohmann::json::object();
    } catch (const YAML::Exception& exception) {
        return Unexpected(exception.what());
    }
}

auto Yaml::ConvertScalar(const YAML::Node& node) -> nlohmann::json {
    const auto& text = node.Scalar();
    if (node.Tag() == "!") {
        return text;
    }
    if (IsNullLiteral(text)) {
        return nullptr;
    }
    if (const auto value = ParseBoolLiteral(text)) {
        return *value;
    }
    if (const auto value = ParseIntegerLiteral(text)) {
        return *value;
    }
    if (const auto value = ParseFloatLiteral(text)) {
        return *value;
    }
    return text;
}

auto Yaml::Convert(const YAML::Node& node) -> nlohmann::json {
    nlohmann::json value;
    switch (node.Type()) {
    case YAML::NodeType::Null:
    case YAML::NodeType::Undefined: value = nullptr; break;
    case YAML::NodeType::Scalar: value = ConvertScalar(node); break;
    case YAML::NodeType::Sequence:
        value = nlohmann::json::array();
        for (const auto& item : node) {
            value.push_back(Convert(item));
        }
        break;
    case YAML::NodeType::Map:
        value = nlohmann::json::object();
        for (const auto& entry : node) {
            const auto key = Convert(entry.first);
            value[key.is_string() ? key.get<std::string>() : key.dump()] = Convert(entry.second);
        }
        break;
    }

    const auto& tag = node.Tag();
    if (tag.size() > 1 && tag.starts_with('!') && !tag.starts_with("!!")) {
        return nlohmann::json{{tag.substr(1), value}};
    }
    return value;
}

auto Yaml::Emit(const nlohmann::json& value) -> std::string {
    YAML::Emitter emitter;
    const auto emit = [&](const auto& self, const nlohmann::json& node) -> void {
        if (node.is_object()) {
            emitter << YAML::BeginMap;
            for (const auto& [key, item] : node.items()) {
                emitter << YAML::Key << key << YAML::Value;
                self(self, item);
            }
            emitter << YAML::EndMap;
        } else if (node.is_array()) {
            emitter << YAML::BeginSeq;
            for (const auto& item : node) {
                self(self, item);
            }
            emitter << YAML::EndSeq;
        } else if (node.is_string()) {
            emitter << node.get<std::string>();
        } else if (node.is_null()) {
            emitter << YAML::Null;
        } else {
            emitter << node.dump();
        }
    };
    emit(emit, value);
    return std::string(emitter.c_str()) + "\n";
}
