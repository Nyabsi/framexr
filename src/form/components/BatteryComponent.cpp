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

#include "BatteryComponent.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>

#include "Devices.hpp"
#include "Json.hpp"

namespace
{

constexpr std::array<std::string_view, 5> kDeviceTags{"", "H", "L", "R", "T"};

} // namespace

#include "document/FormParser.hpp"

auto BatteryComponent::Parse(const nlohmann::json& json) -> BatteryElement
{
    return {
        .font_size = json.at("font_size").get<int>(),
        .fg_color = json.at("fg_color").get<std::string>(),
        .centered = FormParser::Optional<bool>(json, "centered").value_or(false),
        .device = json.at("device").get<size_t>(),
        .low_threshold = json.at("low_threshold").get<float>(),
        .low_color = json.at("low_color").get<std::string>(),
        .charging_color = json.at("charging_color").get<std::string>(),
    };
}

auto BatteryComponent::Serialize(nlohmann::ordered_json& json, const BatteryElement& element) -> void
{
    json["font_size"] = element.font_size;
    json["fg_color"] = element.fg_color;
    if (element.centered)
    {
        json["centered"] = true;
    }
    json["device"] = element.device;
    json["low_threshold"] = Json::ShortestFloat(element.low_threshold);
    json["low_color"] = element.low_color;
    json["charging_color"] = element.charging_color;
}

auto BatteryComponent::Create(FormComponent& parent, const BatteryElement& element, const FormContext& context) -> BatteryComponent&
{
    auto& component = parent.AddComponent<BatteryComponent>("", element.device, element.low_threshold);
    component.SetRect(FormParser::ToRect(element.rect));
    component.Label().SetFontSize(element.font_size);
    component.Label().SetFgColor(FormParser::Color(element.fg_color, context.theme));
    component.Label().SetCentered(element.centered);
    component.SetColors(
        FormParser::Color(element.fg_color, context.theme),
        FormParser::Color(element.low_color, context.theme),
        FormParser::Color(element.charging_color, context.theme)
    );
    return component;
}

BatteryComponent::BatteryComponent(std::string name, size_t device, float low_threshold)
    : FormComponent(std::move(name)),
      m_label(AddComponent<LabelComponent>("label")),
      m_device(device),
      m_low_threshold(low_threshold)
{
}

auto BatteryComponent::SetColors(const Eigen::Vector4f& normal, const Eigen::Vector4f& low, const Eigen::Vector4f& charging) -> void
{
    m_normal_color = normal;
    m_low_color = low;
    m_charging_color = charging;
    m_label.SetFgColor(normal);
}

auto BatteryComponent::Layout() -> void
{
    m_label.SetSize(Size());
}

auto BatteryComponent::Update() -> void
{
    if (m_device >= g_devices->All().size())
    {
        m_label.SetText("");
        return;
    }
    const auto& device = g_devices->All()[m_device];
    if (!device.soc)
    {
        m_label.SetText("");
        m_label.SetFgColor(Eigen::Vector4f::Zero());
        return;
    }
    const auto percent = static_cast<uint32_t>(std::min(*device.soc * 100.0f, 99.0f));
    m_label.SetText(std::format("{}{}", kDeviceTags[static_cast<size_t>(device.role)], percent));
    if (device.charging)
    {
        m_label.SetFgColor(m_charging_color);
    }
    else if (*device.soc < m_low_threshold)
    {
        m_label.SetFgColor(m_low_color);
    }
    else
    {
        m_label.SetFgColor(m_normal_color);
    }
}
