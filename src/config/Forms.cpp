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

#include "Forms.hpp"

#include <algorithm>
#include <format>
#include <system_error>

#include "Config.hpp"
#include "ConfigStore.hpp"
#include "Logger.hpp"
#include "Yaml.hpp"
#include "document/FormParser.hpp"

namespace {

constexpr std::string_view kSettingsName = "settings";

constexpr char kSettingsYaml[] = {
#embed "settings.yaml"
    ,
    '\0'
};

constexpr char kWatchYaml[] = {
#embed "watch.yaml"
    ,
    '\0'
};

} // namespace

auto Forms::Load(const std::string& name) -> Result<FormDocument> {
    if (name == kSettingsName) {
        return ConfigStore::LoadKnownYaml<FormDocument>("settings.yaml", kSettingsYaml);
    }
    const auto filename = std::format("{}.yaml", name);
    const auto text = ConfigStore::LoadText(filename);
    if (!text) {
        return Unexpected(std::format("Could not read file at {}", filename));
    }
    return FormParser::Parse(*text);
}

auto Forms::Watch(std::string_view profile) -> FormDocument {
    if (const auto text = ConfigStore::ReadFile(WatchFile(profile))) {
        if (auto parsed = FormParser::Parse(*text)) {
            return std::move(*parsed);
        } else {
            LOG_ERROR("Watch layout {} is broken: {}", profile, parsed.error().Message());
        }
    } else if (!IsBuiltInWatch(profile)) {
        LOG_WARN("Unknown watch layout {}, using {}", profile, kDefaultWatch);
    }
    auto built_in = FormParser::Parse(kWatchYaml);
    if (!built_in) {
        FatalError(std::format("Built-in watch is broken: {}", built_in.error().Message()));
    }
    return std::move(*built_in);
}

auto Forms::WatchFile(std::string_view profile) -> std::filesystem::path {
    return ConfigStore::Root() / "watches" / std::format("{}.yaml", profile);
}

auto Forms::SaveWatch(std::string_view profile, const FormDocument& document) -> Result<> {
    return ConfigStore::WriteFile(WatchFile(profile), Yaml::Emit(nlohmann::ordered_json(document)));
}

auto Forms::DeleteWatch(std::string_view profile) -> void {
    std::error_code error;
    std::filesystem::remove(WatchFile(profile), error);
}

auto Forms::IsBuiltInWatch(std::string_view profile) -> bool {
    return profile == kDefaultWatch;
}

auto Forms::WatchProfiles() -> std::vector<std::string> {
    std::vector<std::string> mine;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(ConfigStore::Root() / "watches", error)) {
        if (entry.path().extension() == ".yaml" && entry.path().stem() != kDefaultWatch) {
            mine.push_back(entry.path().stem().string());
        }
    }
    std::ranges::sort(mine);
    mine.insert(mine.begin(), std::string(kDefaultWatch));
    return mine;
}
