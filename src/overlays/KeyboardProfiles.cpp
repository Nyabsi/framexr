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

#include "KeyboardProfiles.hpp"

#include <algorithm>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <system_error>

#include "Config.hpp"
#include "Logger.hpp"

namespace {

constexpr char kKeychronV1Yaml[] = {
#embed "keyboards/keychron-v1-ansi.yaml"
    ,
    '\0'
};

constexpr char kFullSizeYaml[] = {
#embed "keyboards/full-size.yaml"
    ,
    '\0'
};

constexpr auto kBuiltInProfiles = std::to_array<std::string_view>({kKeychronV1Yaml, kFullSizeYaml});

} // namespace

KeyboardProfiles* g_keyboard_profiles = nullptr;

auto KeyboardProfiles::Load(std::span<const std::string_view> built_in, const std::filesystem::path& user_directory)
    -> std::vector<KeyboardLayout> {
    std::vector<KeyboardLayout> profiles;
    const auto add = [&profiles](KeyboardLayout layout) -> void {
        const auto existing = std::ranges::find(profiles, layout.Name(), &KeyboardLayout::Name);
        if (existing != profiles.end()) {
            *existing = std::move(layout);
        } else {
            profiles.push_back(std::move(layout));
        }
    };

    for (const auto text : built_in) {
        auto layout = KeyboardLayout::Parse(text);
        if (!layout) {
            FatalError(std::format("Invalid built-in keyboard profile: {}", layout.error().Message()));
        }
        add(std::move(*layout));
    }

    std::error_code error;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(user_directory, error)) {
        const auto extension = entry.path().extension();
        if (entry.is_regular_file(error) && (extension == ".yaml" || extension == ".yml")) {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);
    for (const auto& file : files) {
        const auto text = ConfigStore::ReadFile(file);
        if (!text) {
            LOG_ERROR("Could not read keyboard profile {}", file.string());
            continue;
        }
        auto layout = KeyboardLayout::Parse(*text);
        if (!layout) {
            LOG_ERROR("Skipping keyboard profile {}: {}", file.string(), layout.error().Message());
            continue;
        }
        LOG_INFO("Loaded keyboard profile {} from {}", layout->Name(), file.string());
        layout->SetSource(file);
        add(std::move(*layout));
    }
    return profiles;
}

auto KeyboardProfiles::UserDirectory() -> std::filesystem::path {
    return ConfigStore::Root() / "keyboards";
}

auto KeyboardProfiles::All() -> const std::vector<KeyboardLayout>& {
    if (!m_profiles) {
        m_profiles = Load(kBuiltInProfiles, UserDirectory());
    }
    return *m_profiles;
}

auto KeyboardProfiles::Reload() -> void {
    m_profiles.reset();
}

auto KeyboardProfiles::IsBuiltIn(std::string_view name) -> bool {
    return std::ranges::any_of(kBuiltInProfiles, [name](std::string_view text) -> bool {
        const auto layout = KeyboardLayout::Parse(text);
        return layout && layout->Name() == name;
    });
}

auto KeyboardProfiles::Find(std::string_view name) -> const KeyboardLayout& {
    const auto& profiles = All();
    const auto found = std::ranges::find(profiles, name, &KeyboardLayout::Name);
    if (found == profiles.end()) {
        LOG_WARN("Unknown keyboard profile {}, using {}", name, profiles.front().Name());
        return profiles.front();
    }
    return *found;
}

auto KeyboardProfiles::After(std::string_view name) -> const KeyboardLayout& {
    const auto& profiles = All();
    const auto found = std::ranges::find(profiles, name, &KeyboardLayout::Name);
    if (found == profiles.end() || std::next(found) == profiles.end()) {
        return profiles.front();
    }
    return *std::next(found);
}
