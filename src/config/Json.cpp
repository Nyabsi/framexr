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

#include "Json.hpp"

#include <array>
#include <charconv>

namespace {

const nlohmann::json kNull;

} // namespace

auto Json::ShortestFloat(float value) -> double {
    std::array<char, 32> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    double shortest = 0.0;
    std::from_chars(buffer.data(), end, shortest);
    return shortest;
}

auto Json::VariantName(const nlohmann::json& json) -> std::string {
    if (json.is_object() && json.size() == 1) {
        return json.begin().key();
    }
    return json.get<std::string>();
}

auto Json::VariantPayload(const nlohmann::json& json) -> const nlohmann::json& {
    if (json.is_object() && json.size() == 1) {
        return json.begin().value();
    }
    return kNull;
}

auto nlohmann::adl_serializer<Eigen::Vector3f>::from_json(const nlohmann::json& json, Eigen::Vector3f& value) -> void {
    const auto array = json.get<std::array<float, 3>>();
    value = {array[0], array[1], array[2]};
}

auto nlohmann::adl_serializer<Eigen::Vector3f>::to_json(nlohmann::json& json, const Eigen::Vector3f& value) -> void {
    json = {Json::ShortestFloat(value.x()), Json::ShortestFloat(value.y()), Json::ShortestFloat(value.z())};
}

auto nlohmann::adl_serializer<Eigen::Quaternionf>::from_json(const nlohmann::json& json, Eigen::Quaternionf& value) -> void {
    const auto array = json.get<std::array<float, 4>>();
    value = Eigen::Quaternionf(array[3], array[0], array[1], array[2]);
}

auto nlohmann::adl_serializer<Eigen::Quaternionf>::to_json(nlohmann::json& json, const Eigen::Quaternionf& value) -> void {
    json = {
        Json::ShortestFloat(value.x()),
        Json::ShortestFloat(value.y()),
        Json::ShortestFloat(value.z()),
        Json::ShortestFloat(value.w()),
    };
}

auto nlohmann::adl_serializer<Eigen::Affine3f>::from_json(const nlohmann::json& json, Eigen::Affine3f& value) -> void {
    const auto columns = json.get<std::array<float, 12>>();
    value = Eigen::Affine3f::Identity();
    for (Eigen::Index column = 0; column < 4; ++column) {
        for (Eigen::Index row = 0; row < 3; ++row) {
            value.matrix()(row, column) = columns[static_cast<size_t>(column * 3 + row)];
        }
    }
}

auto nlohmann::adl_serializer<Eigen::Affine3f>::to_json(nlohmann::json& json, const Eigen::Affine3f& value) -> void {
    json = nlohmann::json::array();
    for (Eigen::Index column = 0; column < 4; ++column) {
        for (Eigen::Index row = 0; row < 3; ++row) {
            json.push_back(Json::ShortestFloat(value.matrix()(row, column)));
        }
    }
}
