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

#include "ExecComponent.hpp"

#include <format>

#include "Json.hpp"
#include "Toast.hpp"
#include "document/FormParser.hpp"

auto ExecComponent::Parse(const nlohmann::json& json) -> ExecElement
{
    return {
        .font_size = json.at("font_size").get<int>(),
        .fg_color = json.at("fg_color").get<std::string>(),
        .centered = FormParser::Optional<bool>(json, "centered").value_or(false),
        .command = json.at("command").get<std::vector<std::string>>(),
        .interval = json.at("interval").get<float>(),
    };
}

auto ExecComponent::Serialize(nlohmann::ordered_json& json, const ExecElement& element) -> void
{
    json["font_size"] = element.font_size;
    json["fg_color"] = element.fg_color;
    if (element.centered)
    {
        json["centered"] = true;
    }
    json["command"] = element.command;
    json["interval"] = Json::ShortestFloat(element.interval);
}

auto ExecComponent::Create(FormComponent& parent, const ExecElement& element, const FormContext& context) -> ExecComponent&
{
    auto& component = parent.AddComponent<ExecComponent>("", element.command, element.interval);
    component.SetRect(FormParser::ToRect(element.rect));
    component.Label().SetFontSize(element.font_size);
    component.Label().SetFgColor(FormParser::Color(element.fg_color, context.theme));
    component.Label().SetCentered(element.centered);
    return component;
}

ExecComponent::ExecComponent(std::string name, std::vector<std::string> command, float interval)
    : FormComponent(std::move(name)),
      m_label(AddComponent<LabelComponent>("label")),
      m_command(std::move(command)),
      m_interval(interval),
      m_last_exec(std::chrono::steady_clock::now())
{
}

auto ExecComponent::Layout() -> void
{
    m_label.SetSize(Size());
}

auto ExecComponent::Update() -> void
{
    if (m_child)
    {
        const auto status = m_child->TryWait();
        if (!status)
        {
            m_child.reset();
            Toast::ShowError(std::format("Error checking child process: {}", status.error().Message()));
            return;
        }
        if (!status->has_value())
        {
            return;
        }
        auto child = std::move(*m_child);
        m_child.reset();
        if ((*status)->Success())
        {
            if (const auto output = child.ReadStdout())
            {
                m_label.SetText(*output);
            }
            else
            {
                Toast::ShowError("LabelData::Exec: Failed to read stdout for child process");
            }
            return;
        }
        Toast::ShowError(std::format("LabelData::Exec: Child process exited with code: {}", (*status)->Describe()));
    }

    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<float>(now - m_last_exec).count() <= m_interval)
    {
        return;
    }
    m_last_exec = now;
    if (auto process = Process::Spawn(m_command, {.capture_stdout = true}))
    {
        m_child.emplace(std::move(*process));
    }
    else
    {
        Toast::ShowError(std::format("Failed to spawn process {}: {}", m_command, process.error().Message()));
    }
}
