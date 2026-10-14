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

#include "ClockComponent.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <utility>

#include "Config.hpp"
#include "Logger.hpp"

namespace
{

constexpr float kCityGap = 6.0f;

constexpr float kCityLine = 1.3f;

enum class NumberPadding : uint8_t
{
    Zero,
    Space,
    None,
};

constexpr std::array<std::string_view, 7>
    kWeekdayNames{"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

constexpr std::array<std::string_view, 12> kMonthNames{
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December"
};

auto AppendNumber(std::string& out, int64_t value, int width, NumberPadding padding) -> void
{
    const auto digits = std::to_string(value < 0 ? -value : value);
    const auto sign = value < 0 ? "-" : "";
    const auto fill = static_cast<int>(digits.size()) + (value < 0 ? 1 : 0);
    if (padding != NumberPadding::None && fill < width)
    {
        const auto pad = std::string(static_cast<size_t>(width - fill), padding == NumberPadding::Zero ? '0' : ' ');
        out += padding == NumberPadding::Zero ? std::format("{}{}{}", sign, pad, digits) : std::format("{}{}{}", pad, sign, digits);
        return;
    }
    out += sign;
    out += digits;
}

auto AppendOffset(std::string& out, std::chrono::seconds offset, bool colon) -> void
{
    const auto total = offset.count();
    const auto absolute = total < 0 ? -total : total;
    out += total < 0 ? '-' : '+';
    out += std::format("{:02}", absolute / 3600);
    if (colon)
    {
        out += ':';
    }
    out += std::format("{:02}", (absolute % 3600) / 60);
}

auto IsoWeek(std::chrono::sys_days day) -> std::pair<int, unsigned>
{
    const std::chrono::weekday weekday(day);
    const auto thursday = day + std::chrono::days(3 - static_cast<int>((weekday.c_encoding() + 6) % 7));
    const std::chrono::year_month_day thursday_date(thursday);
    const auto year_start = std::chrono::sys_days(thursday_date.year() / std::chrono::January / 1);
    const auto week = static_cast<unsigned>((thursday - year_start).count() / 7 + 1);
    return {static_cast<int>(thursday_date.year()), week};
}

} // namespace

#include "document/FormParser.hpp"

auto ClockComponent::Parse(const nlohmann::json& json) -> ClockElement
{
    return {
        .font_size = json.at("font_size").get<int>(),
        .fg_color = json.at("fg_color").get<std::string>(),
        .centered = FormParser::Optional<bool>(json, "centered").value_or(false),
        .format = json.at("format").get<std::string>(),
        .timezone = FormParser::Timezone(json),
        .city_size = FormParser::Optional<int>(json, "city_size"),
    };
}

auto ClockComponent::Serialize(nlohmann::ordered_json& json, const ClockElement& element) -> void
{
    json["font_size"] = element.font_size;
    json["fg_color"] = element.fg_color;
    if (element.centered)
    {
        json["centered"] = true;
    }
    json["format"] = element.format;
    if (element.timezone)
    {
        FormParser::PutTimezone(json, *element.timezone);
    }
    if (element.city_size)
    {
        json["city_size"] = *element.city_size;
    }
}

auto ClockComponent::Create(FormComponent& parent, const ClockElement& element, const FormContext& context) -> ClockComponent&
{
    const std::chrono::time_zone* zone = nullptr;
    bool broken = false;
    if (element.timezone)
    {
        if (const auto name = ZoneName(*element.timezone))
        {
            zone = Zone(*name);
            if (zone == nullptr)
            {
                LOG_ERROR("Failed to parse timezone '{}'", *name);
                broken = true;
            }
        }
        else
        {
            broken = true;
        }
    }
    auto& component = parent.AddComponent<ClockComponent>("", element.format, zone);
    component.SetRect(FormParser::ToRect(element.rect));
    component.Label().SetFontSize(element.font_size);
    component.Label().SetFgColor(broken ? FormTheme::FallbackColor() : FormParser::Color(element.fg_color, context.theme));
    component.Label().SetCentered(element.centered);
    if (element.city_size)
    {
        component.SetCity(City(element.timezone), *element.city_size);
    }
    return component;
}

ClockComponent::ClockComponent(std::string name, std::string format, const std::chrono::time_zone* zone)
    : FormComponent(std::move(name)),
      m_label(AddComponent<LabelComponent>("label")),
      m_format(std::move(format)),
      m_zone(zone)
{
}

auto ClockComponent::SetCity(std::string_view city, int font_size) -> void
{
    if (m_city == nullptr)
    {
        m_city = &AddComponent<LabelComponent>("city");
    }
    m_city->SetText(city);
    m_city->SetFontSize(font_size);
}

auto ClockComponent::Zone(std::string_view name) -> const std::chrono::time_zone*
{
    try
    {
        return std::chrono::locate_zone(name);
    }
    catch (const std::runtime_error&)
    {
        return nullptr;
    }
}

auto ClockComponent::ZoneName(const std::variant<size_t, std::string>& zone) -> std::optional<std::string>
{
    if (const auto* index = std::get_if<size_t>(&zone))
    {
        if (*index < g_config->timezones.size())
        {
            return g_config->timezones[*index];
        }
        LOG_ERROR("Timezone index out of range '{}'", *index);
        return std::nullopt;
    }
    return std::get<std::string>(zone);
}

auto ClockComponent::City(const std::optional<std::variant<size_t, std::string>>& zone) -> std::string
{
    std::string name;
    if (!zone)
    {
        try
        {
            name = std::string(std::chrono::current_zone()->name());
        }
        catch (const std::runtime_error&)
        {
            name = "Local";
        }
    }
    else if (const auto* index = std::get_if<size_t>(&*zone))
    {
        name = *index < g_config->timezones.size() ? g_config->timezones[*index] : std::string("Local");
    }
    else
    {
        name = std::get<std::string>(*zone);
    }
    auto city = name.substr(name.rfind('/') == std::string::npos ? 0 : name.rfind('/') + 1);
    std::ranges::replace(city, '_', ' ');
    return city;
}

auto ClockComponent::FormatTime(
    std::string_view format,
    const std::chrono::time_zone* zone,
    std::chrono::system_clock::time_point now
) -> std::string
{
    const auto* effective_zone = zone != nullptr ? zone : std::chrono::current_zone();
    const auto info = effective_zone->get_info(now);
    const auto local = std::chrono::floor<std::chrono::nanoseconds>(now) + std::chrono::nanoseconds(info.offset);
    const auto day = std::chrono::floor<std::chrono::days>(local);
    const std::chrono::year_month_day date(std::chrono::sys_days(day.time_since_epoch()));
    const std::chrono::hh_mm_ss time(local - day);
    const std::chrono::weekday weekday(std::chrono::sys_days(day.time_since_epoch()));

    const auto year = static_cast<int>(date.year());
    const auto month = static_cast<unsigned>(date.month());
    const auto day_of_month = static_cast<unsigned>(date.day());
    const auto hour = static_cast<int>(time.hours().count());
    const auto hour12 = hour % 12 == 0 ? 12 : hour % 12;
    const auto ordinal = (std::chrono::sys_days(date) - std::chrono::sys_days(date.year() / std::chrono::January / 1)).count() + 1;
    const auto days_from_sunday = static_cast<int>(weekday.c_encoding());
    const auto days_from_monday = (days_from_sunday + 6) % 7;
    const auto [iso_year, iso_week] = IsoWeek(std::chrono::sys_days(date));

    std::string out;
    for (size_t index = 0; index < format.size(); ++index)
    {
        if (format[index] != '%' || index + 1 >= format.size())
        {
            out += format[index];
            continue;
        }
        auto spec = format[++index];
        std::optional<NumberPadding> pad_override;
        if ((spec == '-' || spec == '_' || spec == '0') && index + 1 < format.size())
        {
            pad_override = spec == '-' ? NumberPadding::None : (spec == '_' ? NumberPadding::Space : NumberPadding::Zero);
            spec = format[++index];
        }
        const auto number = [&](int64_t value, int width, NumberPadding padding) -> void
        { AppendNumber(out, value, width, pad_override.value_or(padding)); };
        switch (spec)
        {
        case 'Y': number(year, 4, NumberPadding::Zero); break;
        case 'C': number(year / 100, 2, NumberPadding::Zero); break;
        case 'y': number(year % 100, 2, NumberPadding::Zero); break;
        case 'm': number(month, 2, NumberPadding::Zero); break;
        case 'b':
        case 'h': out += kMonthNames[month - 1].substr(0, 3); break;
        case 'B': out += kMonthNames[month - 1]; break;
        case 'd': number(day_of_month, 2, NumberPadding::Zero); break;
        case 'e': number(day_of_month, 2, NumberPadding::Space); break;
        case 'a': out += kWeekdayNames[static_cast<size_t>(days_from_sunday)].substr(0, 3); break;
        case 'A': out += kWeekdayNames[static_cast<size_t>(days_from_sunday)]; break;
        case 'w': number(days_from_sunday, 1, NumberPadding::None); break;
        case 'u': number(days_from_monday + 1, 1, NumberPadding::None); break;
        case 'U': number((ordinal - 1 + 7 - days_from_sunday) / 7, 2, NumberPadding::Zero); break;
        case 'W': number((ordinal - 1 + 7 - days_from_monday) / 7, 2, NumberPadding::Zero); break;
        case 'G': number(iso_year, 4, NumberPadding::Zero); break;
        case 'g': number(iso_year % 100, 2, NumberPadding::Zero); break;
        case 'V': number(iso_week, 2, NumberPadding::Zero); break;
        case 'j': number(ordinal, 3, NumberPadding::Zero); break;
        case 'q': number((month - 1) / 3 + 1, 1, NumberPadding::None); break;
        case 'D':
        case 'x': out += std::format("{:02}/{:02}/{:02}", month, day_of_month, year % 100); break;
        case 'F': out += std::format("{:04}-{:02}-{:02}", year, month, day_of_month); break;
        case 'v': out += std::format("{:>2}-{}-{:04}", day_of_month, kMonthNames[month - 1].substr(0, 3), year); break;
        case 'H': number(hour, 2, NumberPadding::Zero); break;
        case 'k': number(hour, 2, NumberPadding::Space); break;
        case 'I': number(hour12, 2, NumberPadding::Zero); break;
        case 'l': number(hour12, 2, NumberPadding::Space); break;
        case 'M': number(time.minutes().count(), 2, NumberPadding::Zero); break;
        case 'S': number(time.seconds().count(), 2, NumberPadding::Zero); break;
        case 'f': number(time.subseconds().count(), 9, NumberPadding::Zero); break;
        case 'p': out += hour < 12 ? "AM" : "PM"; break;
        case 'P': out += hour < 12 ? "am" : "pm"; break;
        case 'R': out += std::format("{:02}:{:02}", hour, time.minutes().count()); break;
        case 'T':
        case 'X': out += std::format("{:02}:{:02}:{:02}", hour, time.minutes().count(), time.seconds().count()); break;
        case 'r':
            out += std::format(
                "{:02}:{:02}:{:02} {}",
                hour12,
                time.minutes().count(),
                time.seconds().count(),
                hour < 12 ? "AM" : "PM"
            );
            break;
        case 'c':
            out += std::format(
                "{} {} {:>2} {:02}:{:02}:{:02} {}",
                kWeekdayNames[static_cast<size_t>(days_from_sunday)].substr(0, 3),
                kMonthNames[month - 1].substr(0, 3),
                day_of_month,
                hour,
                time.minutes().count(),
                time.seconds().count(),
                year
            );
            break;
        case 'Z':
            if (zone == nullptr)
            {
                AppendOffset(out, info.offset, true);
            }
            else
            {
                out += info.abbrev;
            }
            break;
        case 'z': AppendOffset(out, info.offset, false); break;
        case ':':
            if (index + 1 < format.size() && format[index + 1] == 'z')
            {
                ++index;
                AppendOffset(out, info.offset, true);
            }
            break;
        case 's': number(std::chrono::floor<std::chrono::seconds>(now).time_since_epoch().count(), 1, NumberPadding::None); break;
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case '%': out += '%'; break;
        default: break;
        }
    }
    return out;
}

auto ClockComponent::Layout() -> void
{
    m_label.SetSize(Size());
    if (m_city != nullptr)
    {
        const auto size = static_cast<float>(m_city->FontSize());
        const bool centered = m_label.Centered();
        m_city->SetRect(
            centered ? CanvasRect{0.0f, -size * kCityLine, Size().x(), size * kCityLine}
                     : CanvasRect{0.0f, -(static_cast<float>(m_label.FontSize()) + kCityGap), Size().x(), size * kCityLine}
        );
        m_city->SetFgColor(m_label.FgColor());
        m_city->SetCentered(centered);
    }
}

auto ClockComponent::Update() -> void
{
    m_label.SetText(FormatTime(m_format, m_zone, std::chrono::system_clock::now()));
}
