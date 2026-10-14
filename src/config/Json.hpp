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

#include <Eigen/Geometry>
#include <nlohmann/json.hpp>

class Json
{
  public:
    static auto ShortestFloat(float value) -> double;
    static auto VariantName(const nlohmann::json& json) -> std::string;
    static auto VariantPayload(const nlohmann::json& json) -> const nlohmann::json&;
};

template <>
class nlohmann::adl_serializer<Eigen::Vector3f>
{
  public:
    static auto from_json(const nlohmann::json& json, Eigen::Vector3f& value) -> void;
    static auto to_json(nlohmann::json& json, const Eigen::Vector3f& value) -> void;
};

template <>
class nlohmann::adl_serializer<Eigen::Quaternionf>
{
  public:
    static auto from_json(const nlohmann::json& json, Eigen::Quaternionf& value) -> void;
    static auto to_json(nlohmann::json& json, const Eigen::Quaternionf& value) -> void;
};

template <>
class nlohmann::adl_serializer<Eigen::Affine3f>
{
  public:
    static auto from_json(const nlohmann::json& json, Eigen::Affine3f& value) -> void;
    static auto to_json(nlohmann::json& json, const Eigen::Affine3f& value) -> void;
};
