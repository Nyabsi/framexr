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

#include "panels/TimezonePanel.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "ButtonComponent.hpp"
#include "Config.hpp"
#include "Editor.hpp"
#include "LaserInputComponent.hpp"
#include "StackComponent.hpp"
#include "TextLineComponent.hpp"
#include "TextSearch.hpp"
#include "panels/PanelStyle.hpp"

namespace
{

constexpr size_t kResults = 16;

auto AllZones() -> const std::vector<std::string>&
{
    static const auto zones = []() -> std::vector<std::string>
    {
        std::vector<std::string> names;
        try
        {
            for (const auto& zone : std::chrono::get_tzdb().zones)
            {
                names.emplace_back(zone.name());
            }
        }
        catch (const std::runtime_error&)
        {
            return {};
        }
        std::ranges::sort(names);
        return names;
    }();
    return zones;
}

} // namespace

auto TimezonePanel::ZoneName(const std::optional<std::variant<size_t, std::string>>& zone) -> std::string
{
    std::string name = "Local";
    if (zone)
    {
        if (const auto* index = std::get_if<size_t>(&*zone))
        {
            name = *index < g_config->timezones.size() ? g_config->timezones[*index] : std::string("Local");
        }
        else
        {
            name = std::get<std::string>(*zone);
        }
    }
    std::ranges::replace(name, '_', ' ');
    return name;
}

auto TimezonePanel::LocalZone() -> std::string
{
    try
    {
        return std::string(std::chrono::current_zone()->name());
    }
    catch (const std::runtime_error&)
    {
        return "UTC";
    }
}

TimezonePanel::TimezonePanel(Editor& editor, const Eigen::Vector2f& size)
    : FormComponent("timezones"),
      m_stack(AddComponent<StackComponent>("stack", PanelStyle::kPadding, PanelStyle::kGap))
{
    SetSize(size);
    m_stack.SetSize(size);
    m_stack.SetBgColor(FormTheme::Rgba("@panel"));
    const auto& components = editor.WatchDesign().Document().Components();
    const size_t index = editor.TimezoneElement();
    if (editor.Target() != EditTarget::Watch || index >= components.size())
    {
        return;
    }
    const auto* clock = std::get_if<ClockElement>(&components.at(index));
    const auto* zone_name = std::get_if<TimezoneElement>(&components.at(index));
    if (clock == nullptr && zone_name == nullptr)
    {
        return;
    }
    const auto text = FormTheme::Rgba("@text");
    const auto subtle = FormTheme::Rgba("@subtle");
    const auto surface = FormTheme::Rgba("@surface");
    const auto accent = FormTheme::Rgba("@accent");
    const auto on_accent = FormTheme::Rgba("@on-accent");
    const auto choose = [&editor, index](std::optional<std::string> zone) -> std::function<void()>
    {
        return [&editor, index, zone = std::move(zone)]() -> void
        {
            editor.EditWatch(
                [&editor, index, &zone](FormDocument& document) -> void
                {
                    auto& changed = document.Components().at(index);
                    if (auto* time = std::get_if<ClockElement>(&changed))
                    {
                        time->timezone = zone ? std::optional<std::variant<size_t, std::string>>(*zone) : std::nullopt;
                    }
                    else if (auto* name = std::get_if<TimezoneElement>(&changed))
                    {
                        name->timezone = zone.value_or(LocalZone());
                    }
                    editor.WatchDesign().Fit(document, index);
                }
            );
            editor.Text().End();
            editor.CloseDialog();
        };
    };

    m_stack.AddComponent<TextLineComponent>("title", "Timezone", PanelStyle::kTitleFont, text, PanelStyle::kTitleHeight);

    m_stack.AddComponent<LaserInputComponent>(
        "timezone.search",
        "Search, like helsinki or america",
        editor.Text(),
        editor.TimezoneQuery(),
        [&editor](const std::string& query) -> void { editor.SetTimezoneQuery(query); }
    );

    const bool is_local = clock != nullptr && !clock->timezone.has_value();
    auto& local = m_stack.AddComponent<ButtonComponent>("local", "Local");
    local.SetSize({0.0f, PanelStyle::kRowHeight});
    local.SetFontSize(PanelStyle::kTextFont);
    local.SetFgColor(is_local ? on_accent : text);
    local.SetBgColor(is_local ? accent : surface);
    local.SetEllipsize(true);
    local.SetOnRelease(choose(std::nullopt));

    const auto current = clock != nullptr ? ZoneName(clock->timezone) : ZoneName(zone_name->timezone);
    const bool searching = editor.Text().Editing("timezone.search");
    const auto& query = searching ? editor.Text().Buffer() : editor.TimezoneQuery();
    size_t shown = 0;
    size_t matches = 0;
    for (const auto& zone : AllZones())
    {
        if (!TextSearch::Matches(zone, query))
        {
            continue;
        }
        ++matches;
        if (shown >= kResults)
        {
            continue;
        }
        auto display = zone;
        std::ranges::replace(display, '_', ' ');
        const bool chosen = display == current;
        auto& row = m_stack.AddComponent<ButtonComponent>(zone, display);
        row.SetSize({0.0f, PanelStyle::kCompactRow});
        row.SetFontSize(PanelStyle::kTextFont);
        row.SetFgColor(chosen ? on_accent : text);
        row.SetBgColor(chosen ? accent : surface);
        row.SetEllipsize(true);
        row.SetOnRelease(choose(zone));
        ++shown;
    }
    if (matches > shown)
    {
        const auto more = std::format("{} more, keep typing to narrow it down", matches - shown);
        m_stack.AddComponent<TextLineComponent>("more", more, PanelStyle::kCaptionFont, subtle, PanelStyle::kCaptionHeight);
    }
    else if (matches == 0)
    {
        m_stack.AddComponent<TextLineComponent>(
            "none",
            "No timezone matches that",
            PanelStyle::kCaptionFont,
            subtle,
            PanelStyle::kCaptionHeight
        );
    }

    auto& done = m_stack.AddComponent<ButtonComponent>("done", "Done");
    done.SetSize({0.0f, PanelStyle::kRowHeight});
    done.SetFontSize(PanelStyle::kTextFont);
    done.SetFgColor(text);
    done.SetBgColor(surface);
    done.SetEllipsize(true);
    done.SetOnRelease(
        [&editor]() -> void
        {
            editor.Text().End();
            editor.CloseDialog();
        }
    );
}
