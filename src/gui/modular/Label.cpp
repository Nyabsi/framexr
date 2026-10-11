// FrameXR
//
// This is a port of wlx-overlay-s from Rust to C++.
//
// Copyright (C) 2026 Nyabsi <nyabsi@sovellus.cc>
// Copyright (C) 2023-2025 galister and wlx-overlay-s contributors
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

#include "Label.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

#include "Canvas.hpp"
#include "Json.hpp"
#include "Logger.hpp"
#include "State.hpp"

namespace {

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
constexpr std::array<std::string_view, 5> kDeviceTags{"", "H", "L", "R", "T"};

auto AppendNumber(std::string& out, int64_t value, int width, NumberPadding padding) -> void {
    const auto digits = std::to_string(value < 0 ? -value : value);
    const auto sign = value < 0 ? "-" : "";
    const auto fill = static_cast<int>(digits.size()) + (value < 0 ? 1 : 0);
    if (padding != NumberPadding::None && fill < width) {
        const auto pad = std::string(static_cast<size_t>(width - fill), padding == NumberPadding::Zero ? '0' : ' ');
        out += padding == NumberPadding::Zero ? std::format("{}{}{}", sign, pad, digits) : std::format("{}{}{}", pad, sign, digits);
        return;
    }
    out += sign;
    out += digits;
}

auto AppendOffset(std::string& out, std::chrono::seconds offset, bool colon) -> void {
    const auto total = offset.count();
    const auto absolute = total < 0 ? -total : total;
    out += total < 0 ? '-' : '+';
    out += std::format("{:02}", absolute / 3600);
    if (colon) {
        out += ':';
    }
    out += std::format("{:02}", (absolute % 3600) / 60);
}

auto IsoWeek(std::chrono::sys_days day) -> std::pair<int, unsigned> {
    const std::chrono::weekday weekday(day);
    const auto thursday = day + std::chrono::days(3 - static_cast<int>((weekday.c_encoding() + 6) % 7));
    const std::chrono::year_month_day thursday_date(thursday);
    const auto year_start = std::chrono::sys_days(thursday_date.year() / std::chrono::January / 1);
    const auto week = static_cast<unsigned>((thursday - year_start).count() / 7 + 1);
    return {static_cast<int>(thursday_date.year()), week};
}

auto ParseTimezone(std::string_view name) -> const std::chrono::time_zone* {
    try {
        return std::chrono::locate_zone(name);
    } catch (const std::runtime_error&) {
        return nullptr;
    }
}

} // namespace

auto from_json(const nlohmann::json& json, LabelContent& value) -> void {
    const auto source = json.at("source").get<std::string>();
    if (source == "Static") {
        value = StaticLabelContent{.text = json.at("text").get<std::string>()};
    } else if (source == "Exec") {
        value = ExecLabelContent{
            .command = json.at("command").get<std::vector<std::string>>(),
            .interval = json.at("interval").get<float>()
        };
    } else if (source == "Clock") {
        ClockLabelContent clock{.format = json.at("format").get<std::string>()};
        if (const auto it = json.find("city_size"); it != json.end() && !it->is_null()) {
            clock.city_size = it->get<int>();
        }
        if (const auto it = json.find("timezone"); it != json.end() && !it->is_null()) {
            if (it->is_number_unsigned() || it->is_number_integer()) {
                clock.timezone = it->get<size_t>();
            } else {
                clock.timezone = it->get<std::string>();
            }
        }
        value = clock;
    } else if (source == "Timezone") {
        const auto& zone = json.at("timezone");
        if (zone.is_number()) {
            value = TimezoneLabelContent{.timezone = zone.get<size_t>()};
        } else {
            value = TimezoneLabelContent{.timezone = zone.get<std::string>()};
        }
    } else if (source == "Timer") {
        value = TimerLabelContent{.format = json.at("format").get<std::string>()};
    } else if (source == "Battery") {
        value = BatteryLabelContent{
            .device = json.at("device").get<size_t>(),
            .low_threshold = json.at("low_threshold").get<float>(),
            .low_color = json.at("low_color").get<std::string>(),
            .charging_color = json.at("charging_color").get<std::string>(),
        };
    } else if (source == "DragMultiplier") {
        value = DragMultiplierLabelContent{};
    } else if (source == "Ipd") {
        value = IpdLabelContent{};
    } else {
        throw nlohmann::json::other_error::create(501, std::format("unknown variant `{}` for label source", source), &json);
    }
}

auto to_json(nlohmann::ordered_json& json, const LabelContent& value) -> void {
    json = nlohmann::ordered_json::object();
    std::visit(
        [&json](const auto& label) -> void {
            using Content = std::decay_t<decltype(label)>;
            if constexpr (std::is_same_v<Content, StaticLabelContent>) {
                json["source"] = "Static";
                json["text"] = label.text;
            } else if constexpr (std::is_same_v<Content, ExecLabelContent>) {
                json["source"] = "Exec";
                json["command"] = label.command;
                json["interval"] = Json::ShortestFloat(label.interval);
            } else if constexpr (std::is_same_v<Content, ClockLabelContent>) {
                json["source"] = "Clock";
                json["format"] = label.format;
                if (label.timezone) {
                    if (const auto* index = std::get_if<size_t>(&*label.timezone)) {
                        json["timezone"] = *index;
                    } else {
                        json["timezone"] = std::get<std::string>(*label.timezone);
                    }
                }
                if (label.city_size) {
                    json["city_size"] = *label.city_size;
                }
            } else if constexpr (std::is_same_v<Content, TimezoneLabelContent>) {
                json["source"] = "Timezone";
                if (const auto* index = std::get_if<size_t>(&label.timezone)) {
                    json["timezone"] = *index;
                } else {
                    json["timezone"] = std::get<std::string>(label.timezone);
                }
            } else if constexpr (std::is_same_v<Content, TimerLabelContent>) {
                json["source"] = "Timer";
                json["format"] = label.format;
            } else if constexpr (std::is_same_v<Content, BatteryLabelContent>) {
                json["source"] = "Battery";
                json["device"] = label.device;
                json["low_threshold"] = Json::ShortestFloat(label.low_threshold);
                json["low_color"] = label.low_color;
                json["charging_color"] = label.charging_color;
            } else if constexpr (std::is_same_v<Content, DragMultiplierLabelContent>) {
                json["source"] = "DragMultiplier";
            } else {
                json["source"] = "Ipd";
            }
        },
        value
    );
}

auto Labels::Create(const LabelContent& content, const AppState& app, const Eigen::Vector4f& fg_color) -> LabelSetup {
    const auto parse_color = [](const std::string& color) -> Eigen::Vector4f {
        if (auto parsed = Canvas::ParseColor(color)) {
            return *parsed;
        } else {
            LOG_ERROR("Failed to parse color '{}': {}", color, parsed.error().Message());
            return Canvas::FallbackColor();
        }
    };

    return std::visit(
        [&](const auto& label) -> LabelSetup {
            using Content = std::decay_t<decltype(label)>;
            if constexpr (std::is_same_v<Content, StaticLabelContent>) {
                return {.text = label.text};
            } else if constexpr (std::is_same_v<Content, ExecLabelContent>) {
                return {.control = std::make_unique<ExecLabel>(label.command, label.interval)};
            } else if constexpr (std::is_same_v<Content, ClockLabelContent>) {
                std::optional<Eigen::Vector4f> color;
                std::optional<std::string> zone_name;
                if (label.timezone) {
                    if (const auto* index = std::get_if<size_t>(&*label.timezone)) {
                        if (*index < app.config.timezones.size()) {
                            zone_name = app.config.timezones[*index];
                        } else {
                            LOG_ERROR("Timezone index out of range '{}'", *index);
                            color = Canvas::FallbackColor();
                        }
                    } else {
                        zone_name = std::get<std::string>(*label.timezone);
                    }
                }
                const std::chrono::time_zone* zone = nullptr;
                if (zone_name) {
                    zone = ParseTimezone(*zone_name);
                    if (zone == nullptr) {
                        LOG_ERROR("Failed to parse timezone '{}'", *zone_name);
                        color = Canvas::FallbackColor();
                    }
                }
                return {.control = std::make_unique<ClockLabel>(label.format, zone), .color = color};
            } else if constexpr (std::is_same_v<Content, TimezoneLabelContent>) {
                std::string zone;
                if (const auto* index = std::get_if<size_t>(&label.timezone)) {
                    if (*index >= app.config.timezones.size()) {
                        LOG_ERROR("Timezone index out of range '{}'", *index);
                        return {.text = "Error", .color = Canvas::FallbackColor()};
                    }
                    zone = app.config.timezones[*index];
                } else {
                    zone = std::get<std::string>(label.timezone);
                }
                auto pretty = zone.substr(zone.rfind('/') == std::string::npos ? 0 : zone.rfind('/') + 1);
                std::ranges::replace(pretty, '_', ' ');
                return {.text = pretty};
            } else if constexpr (std::is_same_v<Content, TimerLabelContent>) {
                return {.control = std::make_unique<TimerLabel>(label.format)};
            } else if constexpr (std::is_same_v<Content, BatteryLabelContent>) {
                return {
                    .control = std::make_unique<BatteryLabel>(
                        label.device,
                        label.low_threshold,
                        fg_color,
                        parse_color(label.low_color),
                        parse_color(label.charging_color)
                    )
                };
            } else if constexpr (std::is_same_v<Content, DragMultiplierLabelContent>) {
                return {.control = std::make_unique<DragMultiplierLabel>()};
            } else {
                return {.control = std::make_unique<IpdLabel>()};
            }
        },
        content
    );
}

auto Labels::City(const std::optional<std::variant<size_t, std::string>>& zone, const AppState& app) -> std::string {
    std::string name;
    if (!zone) {
        try {
            name = std::string(std::chrono::current_zone()->name());
        } catch (const std::runtime_error&) {
            name = "Local";
        }
    } else if (const auto* index = std::get_if<size_t>(&*zone)) {
        name = *index < app.config.timezones.size() ? app.config.timezones[*index] : std::string("Local");
    } else {
        name = std::get<std::string>(*zone);
    }
    auto city = name.substr(name.rfind('/') == std::string::npos ? 0 : name.rfind('/') + 1);
    std::ranges::replace(city, '_', ' ');
    return city;
}

auto Labels::FormatTime(std::string_view format, const std::chrono::time_zone* zone, std::chrono::system_clock::time_point now)
    -> std::string {
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
    for (size_t index = 0; index < format.size(); ++index) {
        if (format[index] != '%' || index + 1 >= format.size()) {
            out += format[index];
            continue;
        }
        auto spec = format[++index];
        std::optional<NumberPadding> pad_override;
        if ((spec == '-' || spec == '_' || spec == '0') && index + 1 < format.size()) {
            pad_override = spec == '-' ? NumberPadding::None : (spec == '_' ? NumberPadding::Space : NumberPadding::Zero);
            spec = format[++index];
        }
        const auto number = [&](int64_t value, int width, NumberPadding padding) -> void {
            AppendNumber(out, value, width, pad_override.value_or(padding));
        };
        switch (spec) {
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
            if (zone == nullptr) {
                AppendOffset(out, info.offset, true);
            } else {
                out += info.abbrev;
            }
            break;
        case 'z': AppendOffset(out, info.offset, false); break;
        case ':':
            if (index + 1 < format.size() && format[index + 1] == 'z') {
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

BatteryLabel::BatteryLabel(size_t device, float low_threshold, Eigen::Vector4f normal, Eigen::Vector4f low, Eigen::Vector4f charging)
    : device_(device),
      low_threshold_(low_threshold),
      normal_color_(std::move(normal)),
      low_color_(std::move(low)),
      charging_color_(std::move(charging)) {
}

auto BatteryLabel::Update(AppState& app) -> void {
    if (device_ >= app.input.devices.size()) {
        SetText("");
        return;
    }
    const auto& device = app.input.devices[device_];
    if (!device.soc) {
        SetText("");
        SetFgColor(Eigen::Vector4f::Zero());
        return;
    }
    const auto percent = static_cast<uint32_t>(std::min(*device.soc * 100.0f, 99.0f));
    SetText(std::format("{}{}", kDeviceTags[static_cast<size_t>(device.role)], percent));
    if (device.charging) {
        SetFgColor(charging_color_);
    } else if (*device.soc < low_threshold_) {
        SetFgColor(low_color_);
    } else {
        SetFgColor(normal_color_);
    }
}

ClockLabel::ClockLabel(std::string format, const std::chrono::time_zone* zone) : format_(std::move(format)), zone_(zone) {
}

auto ClockLabel::Update(AppState&) -> void {
    SetText(Labels::FormatTime(format_, zone_, std::chrono::system_clock::now()));
}

TimerLabel::TimerLabel(std::string format) : format_(std::move(format)), start_(std::chrono::steady_clock::now()) {
}

auto TimerLabel::Update(AppState&) -> void {
    auto formatted = format_;
    std::ranges::transform(formatted, formatted.begin(), [](unsigned char c) -> char {
        return static_cast<char>(std::tolower(c));
    });
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_).count();
    const auto replace_all = [&](std::string_view token, const std::string& replacement) -> void {
        for (auto position = formatted.find(token); position != std::string::npos;
             position = formatted.find(token, position + replacement.size())) {
            formatted.replace(position, token.size(), replacement);
        }
    };
    replace_all("%s", std::format("{:02}", elapsed % 60));
    replace_all("%m", std::format("{:02}", (elapsed / 60) % 60));
    replace_all("%h", std::format("{:02}", (elapsed / 60) / 60));
    SetText(formatted);
}

ExecLabel::ExecLabel(std::vector<std::string> command, float interval)
    : command_(std::move(command)),
      interval_(interval),
      last_exec_(std::chrono::steady_clock::now()) {
}

auto ExecLabel::Update(AppState& app) -> void {
    if (child_) {
        const auto status = child_->TryWait();
        if (!status) {
            child_.reset();
            Toast::ShowError(app, std::format("Error checking child process: {}", status.error().Message()));
            return;
        }
        if (!status->has_value()) {
            return;
        }
        auto child = std::move(*child_);
        child_.reset();
        if ((*status)->Success()) {
            if (const auto output = child.ReadStdout()) {
                SetText(*output);
            } else {
                Toast::ShowError(app, "LabelData::Exec: Failed to read stdout for child process");
            }
            return;
        }
        Toast::ShowError(app, std::format("LabelData::Exec: Child process exited with code: {}", (*status)->Describe()));
    }

    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<float>(now - last_exec_).count() <= interval_) {
        return;
    }
    last_exec_ = now;
    if (auto process = Process::Spawn(command_, {.capture_stdout = true})) {
        child_.emplace(std::move(*process));
    } else {
        Toast::ShowError(app, std::format("Failed to spawn process {}: {}", command_, process.error().Message()));
    }
}

auto IpdLabel::Update(AppState& app) -> void {
    if (std::abs(app.input.ipd - last_ipd_) > 0.05f) {
        last_ipd_ = app.input.ipd;
        SetText(std::format("{:.1f}", app.input.ipd));
    }
}

auto DragMultiplierLabel::Update(AppState& app) -> void {
    SetText(std::format("{:.1f}", app.config.space_drag_multiplier));
}
