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

#include "TimerComponent.hpp"

#include <algorithm>
#include <cctype>
#include <format>

#include "document/FormParser.hpp"

auto TimerComponent::Parse(const nlohmann::json& json) -> TimerElement
{
    return {
        .font_size = json.at("font_size").get<int>(),
        .fg_color = json.at("fg_color").get<std::string>(),
        .centered = FormParser::Optional<bool>(json, "centered").value_or(false),
        .format = json.at("format").get<std::string>(),
    };
}

auto TimerComponent::Serialize(nlohmann::ordered_json& json, const TimerElement& element) -> void
{
    json["font_size"] = element.font_size;
    json["fg_color"] = element.fg_color;
    if (element.centered)
    {
        json["centered"] = true;
    }
    json["format"] = element.format;
}

auto TimerComponent::Create(FormComponent& parent, const TimerElement& element, const FormContext& context) -> TimerComponent&
{
    auto& component = parent.AddComponent<TimerComponent>("", element.format);
    component.SetRect(FormParser::ToRect(element.rect));
    component.Label().SetFontSize(element.font_size);
    component.Label().SetFgColor(FormParser::Color(element.fg_color, context.theme));
    component.Label().SetCentered(element.centered);
    return component;
}

TimerComponent::TimerComponent(std::string name, std::string format)
    : FormComponent(std::move(name)),
      m_label(AddComponent<LabelComponent>("label")),
      m_format(std::move(format)),
      m_start(std::chrono::steady_clock::now())
{
}

auto TimerComponent::Layout() -> void
{
    m_label.SetSize(Size());
}

auto TimerComponent::Update() -> void
{
    auto formatted = m_format;
    std::ranges::transform(formatted, formatted.begin(), [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_start).count();
    const auto replace_all = [&](std::string_view token, const std::string& replacement) -> void
    {
        for (auto position = formatted.find(token); position != std::string::npos;
             position = formatted.find(token, position + replacement.size()))
        {
            formatted.replace(position, token.size(), replacement);
        }
    };
    replace_all("%s", std::format("{:02}", elapsed % 60));
    replace_all("%m", std::format("{:02}", (elapsed / 60) % 60));
    replace_all("%h", std::format("{:02}", (elapsed / 60) / 60));
    m_label.SetText(formatted);
}
