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

#include "Bindings.hpp"

#include <algorithm>
#include <format>
#include <string_view>
#include <utility>

#include "Config.hpp"
#include "ConfigStore.hpp"
#include "Logger.hpp"

namespace {

constexpr unsigned char kDefaultProfiles[] = {
#embed "openxr_actions.json"
};

constexpr auto kProfileFields = std::to_array<std::pair<std::string_view, std::optional<InputBinding> BindingProfile::*>>({
    {"pose", &BindingProfile::pose},
    {"click", &BindingProfile::click},
    {"grab", &BindingProfile::grab},
    {"alt_click", &BindingProfile::alt_click},
    {"space_drag", &BindingProfile::space_drag},
    {"space_rotate", &BindingProfile::space_rotate},
    {"space_reset", &BindingProfile::space_reset},
    {"click_modifier_right", &BindingProfile::click_modifier_right},
    {"click_modifier_middle", &BindingProfile::click_modifier_middle},
    {"move_mouse", &BindingProfile::move_mouse},
    {"scroll", &BindingProfile::scroll},
    {"haptic", &BindingProfile::haptic},
});

auto ParseBinding(const nlohmann::json& json) -> Result<InputBinding> {
    if (!json.is_object()) {
        return Unexpected("invalid type: expected struct OpenXrActionConfAction");
    }
    InputBinding binding;
    for (const auto& [key, member] : {std::pair{"left", &InputBinding::left}, std::pair{"right", &InputBinding::right}}) {
        if (const auto it = json.find(key); it != json.end() && !it->is_null()) {
            if (!it->is_string()) {
                return Unexpected(std::format("invalid type for `{}`: expected a string", key));
            }
            binding.*member = it->get<std::string>();
        }
    }
    if (const auto it = json.find("threshold"); it != json.end() && !it->is_null()) {
        if (!it->is_array() || it->size() != 2 || !(*it)[0].is_number() || !(*it)[1].is_number()) {
            return Unexpected("invalid type for `threshold`: expected an array of 2 numbers");
        }
        binding.threshold = std::array{(*it)[0].get<float>(), (*it)[1].get<float>()};
    }
    for (const auto& [key, member] :
         {std::pair{"double_click", &InputBinding::double_click}, std::pair{"triple_click", &InputBinding::triple_click}}) {
        if (const auto it = json.find(key); it != json.end() && !it->is_null()) {
            if (!it->is_boolean()) {
                return Unexpected(std::format("invalid type for `{}`: expected a boolean", key));
            }
            binding.*member = it->get<bool>();
        }
    }
    return binding;
}

} // namespace

auto Bindings::Parse(const nlohmann::json& json) -> Result<std::vector<BindingProfile>> {
    if (!json.is_array()) {
        return Unexpected("invalid type: expected a sequence");
    }
    std::vector<BindingProfile> profiles;
    for (const auto& entry : json) {
        if (!entry.is_object()) {
            return Unexpected("invalid type: expected struct OpenXrActionConfProfile");
        }
        const auto profile = entry.find("profile");
        if (profile == entry.end() || !profile->is_string()) {
            return Unexpected("missing field `profile`");
        }
        BindingProfile parsed{.profile = profile->get<std::string>()};
        for (const auto& [key, member] : kProfileFields) {
            if (const auto it = entry.find(key); it != entry.end() && !it->is_null()) {
                ASSIGN_OR_RETURN(parsed.*member, ParseBinding(*it));
            }
        }
        profiles.push_back(std::move(parsed));
    }
    return profiles;
}

auto Bindings::Load() -> std::vector<BindingProfile> {
    const std::string_view defaults(reinterpret_cast<const char*>(kDefaultProfiles), sizeof(kDefaultProfiles));
    auto profiles = Parse(nlohmann::json::parse(defaults, nullptr, false, true));
    if (!profiles) {
        FatalError(std::format("Invalid built-in openxr_actions.json: {}", profiles.error().Message()));
    }

    const auto override_text = ConfigStore::LoadText("openxr_actions.json");
    if (!override_text) {
        return *profiles;
    }
    const auto json = nlohmann::json::parse(*override_text, nullptr, false, true);
    auto overrides = json.is_discarded() ? Result<std::vector<BindingProfile>>(Unexpected("invalid JSON")) : Parse(json);
    if (!overrides) {
        LOG_ERROR("Failed to load openxr_actions.json: {}", overrides.error().Message());
        return *profiles;
    }
    for (auto& replacement : *overrides) {
        const auto existing = std::ranges::find(*profiles, replacement.profile, &BindingProfile::profile);
        if (existing != profiles->end()) {
            *existing = std::move(replacement);
        }
    }
    return *profiles;
}
