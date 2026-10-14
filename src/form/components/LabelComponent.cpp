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

#include "LabelComponent.hpp"

#include <cmath>

#include "document/FormParser.hpp"

auto LabelComponent::Parse(const nlohmann::json& json) -> LabelElement
{
    return {
        .font_size = json.at("font_size").get<int>(),
        .fg_color = json.at("fg_color").get<std::string>(),
        .centered = FormParser::Optional<bool>(json, "centered").value_or(false),
        .text = json.at("text").get<std::string>(),
    };
}

auto LabelComponent::Serialize(nlohmann::ordered_json& json, const LabelElement& element) -> void
{
    json["font_size"] = element.font_size;
    json["fg_color"] = element.fg_color;
    if (element.centered)
    {
        json["centered"] = true;
    }
    json["text"] = element.text;
}

auto LabelComponent::Create(FormComponent& parent, const LabelElement& element, const FormContext& context) -> LabelComponent&
{
    auto& label = parent.AddComponent<LabelComponent>("", element.text);
    label.SetRect(FormParser::ToRect(element.rect));
    label.SetFontSize(element.font_size);
    label.SetFgColor(FormParser::Color(element.fg_color, context.theme));
    label.SetCentered(element.centered);
    return label;
}

LabelComponent::LabelComponent(std::string name, std::string_view text) : FormComponent(std::move(name)), m_text(text)
{
}

auto LabelComponent::SetText(std::string_view text) -> void
{
    if (m_text != text)
    {
        m_text = text;
        MarkDirty();
    }
}

auto LabelComponent::SetFontSize(int font_size) -> void
{
    m_font_size = font_size;
    MarkDirty();
}

auto LabelComponent::SetFgColor(const Eigen::Vector4f& fg_color) -> void
{
    if (m_fg_color != fg_color)
    {
        m_fg_color = fg_color;
        MarkDirty();
    }
}

auto LabelComponent::SetCentered(bool centered) -> void
{
    m_centered = centered;
    MarkDirty();
}

auto LabelComponent::Emit(FormState& state, const FormFrame& frame) -> void
{
    state.push_back(
        TextItem{
            .rect = frame.rect,
            .text = m_text,
            .font_size = static_cast<int>(std::lround(static_cast<float>(m_font_size) * frame.scale)),
            .color = m_fg_color,
            .centered = m_centered,
            .owner = this,
        }
    );
}
