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

#include "OpenVrManifest.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>

#include <nlohmann/json.hpp>
#include <openvr.h>

#include "Config.hpp"
#include "Logger.hpp"
#include "Platform.hpp"

namespace {

constexpr const char* kAppKey = "nyabsi.framexr";

auto ManifestPath() -> std::filesystem::path {
    return Config::Root() / "framexr.vrmanifest";
}

#ifdef _WIN32
constexpr const char* kBinaryPathKey = "binary_path_windows";
#else
constexpr const char* kBinaryPathKey = "binary_path_linux";
#endif

auto ExecutablePath() -> Result<std::string> {
    ASSIGN_OR_RETURN(const auto path, Platform::ExecutablePath());
    return path.string();
}

auto ApplicationError(vr::EVRApplicationError error) -> const char* {
    return vr::VRApplications()->GetApplicationsErrorNameFromEnum(error);
}

} // namespace

auto OpenVrManifest::Install() -> Result<> {
    const auto manifest_path = ManifestPath();
    ASSIGN_OR_RETURN(const auto executable_path, ExecutablePath());

    if (vr::VRApplications()->IsApplicationInstalled(kAppKey)) {
        if (const auto contents = Config::ReadFile(manifest_path)) {
            const auto manifest = nlohmann::json::parse(*contents, nullptr, false);
            if (manifest.is_discarded()) {
                return Unexpected(std::format("Invalid manifest at {}", manifest_path.string()));
            }
            const auto& binary = manifest["applications"][0][kBinaryPathKey];
            if (binary.is_string() && binary.get<std::string>() == executable_path) {
                LOG_INFO("Manifest already up to date");
                return {};
            }
        }
    }

    nlohmann::ordered_json strings;
    strings["name"] = "FrameXR";
    strings["description"] = "A lightweight Wayland desktop overlay for OpenVR/OpenXR";

    nlohmann::ordered_json application;
    application["app_key"] = kAppKey;
    application["launch_type"] = "binary";
    application[kBinaryPathKey] = executable_path;
    application["is_dashboard_overlay"] = true;
    application["strings"]["en_us"] = strings;

    nlohmann::ordered_json manifest;
    manifest["source"] = "builtin";
    manifest["applications"] = nlohmann::ordered_json::array({application});

    std::ofstream stream(manifest_path, std::ios::trunc);
    if (!stream) {
        return Unexpected(std::format("Failed to create manifest file at {}", manifest_path.string()));
    }
    stream << manifest.dump();
    if (!stream) {
        return Unexpected(std::format("Failed to write manifest file at {}", manifest_path.string()));
    }
    stream.close();

    if (const auto error = vr::VRApplications()->AddApplicationManifest(manifest_path.string().c_str(), false);
        error != vr::VRApplicationError_None) {
        return Unexpected(std::format("Failed to add manifest to OpenVR: {}", ApplicationError(error)));
    }
    if (const auto error = vr::VRApplications()->SetApplicationAutoLaunch(kAppKey, true); error != vr::VRApplicationError_None) {
        return Unexpected(std::format("Failed to set auto launch: {}", ApplicationError(error)));
    }
    return {};
}

auto OpenVrManifest::Uninstall() -> Result<> {
    if (!vr::VRApplications()->IsApplicationInstalled(kAppKey)) {
        return {};
    }
    if (const auto error = vr::VRApplications()->RemoveApplicationManifest(ManifestPath().string().c_str());
        error != vr::VRApplicationError_None) {
        return Unexpected(std::format("Failed to remove manifest from OpenVR: {}", ApplicationError(error)));
    }
    LOG_INFO("Uninstalled manifest");
    return {};
}
