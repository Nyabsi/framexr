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

#include "Monado.hpp"

#include <cstdlib>
#include <format>
#include <optional>
#include <ranges>

#include <dlfcn.h>
#include <link.h>
#include <nlohmann/json.hpp>

#include "Config.hpp"
#include "ConfigStore.hpp"

namespace
{

auto RoleName(MonadoDeviceRole role) -> const char*
{
    switch (role)
    {
    case MonadoDeviceRole::Head: return "head";
    case MonadoDeviceRole::Eyes: return "eyes";
    case MonadoDeviceRole::Left: return "left";
    case MonadoDeviceRole::Right: return "right";
    case MonadoDeviceRole::Gamepad: return "gamepad";
    case MonadoDeviceRole::HandTrackingLeft: return "hand-tracking-left";
    case MonadoDeviceRole::HandTrackingRight: return "hand-tracking-right";
    }
    return "";
}

auto ToMonadoResult(int32_t result) -> std::expected<void, MonadoResult>
{
    if (result == 0)
    {
        return {};
    }
    return std::unexpected(static_cast<MonadoResult>(result));
}

template <typename Function>
auto LoadSymbol(void* library, const char* name, Function& function) -> bool
{
    function = reinterpret_cast<Function>(dlsym(library, name));
    return function != nullptr;
}

auto LoadApi(void* library) -> std::optional<MonadoApi>
{
    MonadoApi api;
    const bool loaded = LoadSymbol(library, "mnd_api_get_version", api.get_version)
                     && LoadSymbol(library, "mnd_root_create", api.root_create)
                     && LoadSymbol(library, "mnd_root_destroy", api.root_destroy)
                     && LoadSymbol(library, "mnd_root_update_client_list", api.update_client_list)
                     && LoadSymbol(library, "mnd_root_get_number_clients", api.get_number_clients)
                     && LoadSymbol(library, "mnd_root_get_client_id_at_index", api.get_client_id_at_index)
                     && LoadSymbol(library, "mnd_root_get_client_name", api.get_client_name)
                     && LoadSymbol(library, "mnd_root_get_client_state", api.get_client_state)
                     && LoadSymbol(library, "mnd_root_toggle_client_io_active", api.toggle_client_io_active)
                     && LoadSymbol(library, "mnd_root_get_device_count", api.get_device_count)
                     && LoadSymbol(library, "mnd_root_get_device_info", api.get_device_info)
                     && LoadSymbol(library, "mnd_root_get_device_from_role", api.get_device_from_role)
                     && LoadSymbol(library, "mnd_root_get_device_battery_status", api.get_device_battery_status)
                     && LoadSymbol(library, "mnd_root_get_reference_space_offset", api.get_reference_space_offset)
                     && LoadSymbol(library, "mnd_root_set_reference_space_offset", api.set_reference_space_offset);
    if (!loaded)
    {
        return std::nullopt;
    }
    return api;
}

auto FindSystemLibrary(const std::string& name) -> std::optional<std::filesystem::path>
{
    void* handle = dlopen(name.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (handle == nullptr)
    {
        return std::nullopt;
    }
    link_map* map = nullptr;
    std::optional<std::filesystem::path> path;
    if (dlinfo(handle, RTLD_DI_LINKMAP, static_cast<void*>(&map)) == 0 && map != nullptr && map->l_name != nullptr)
    {
        path = std::filesystem::path(map->l_name);
    }
    dlclose(handle);
    return path;
}

auto ResolveRuntimeLibrary(const std::filesystem::path& library, const std::filesystem::path& runtime_json)
    -> Result<std::filesystem::path>
{
    std::error_code error;
    auto directory = std::filesystem::canonical(runtime_json, error);
    if (error)
    {
        return Unexpected(std::format("Failed to canonicalize runtime json path: {}", error.message()));
    }
    directory = directory.parent_path();
    auto relative = directory / library;

    if (std::distance(library.begin(), library.end()) > 1)
    {
        return relative;
    }
    if (auto system_path = FindSystemLibrary(library.string()))
    {
        return *system_path;
    }
    return relative;
}

auto RuntimeJsonCandidates() -> std::vector<std::filesystem::path>
{
    std::vector<std::filesystem::path> candidates;
    if (const char* override_path = std::getenv("XR_RUNTIME_JSON"))
    {
        candidates.emplace_back(override_path);
    }

    std::vector<std::filesystem::path> config_dirs;
    if (const char* config_home = std::getenv("XDG_CONFIG_HOME"); config_home != nullptr && *config_home == '/')
    {
        config_dirs.emplace_back(config_home);
    }
    else if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0')
    {
        config_dirs.emplace_back(std::filesystem::path(home) / ".config");
    }
    const char* dirs = std::getenv("XDG_CONFIG_DIRS");
    const std::string_view dirs_view = dirs != nullptr && *dirs != '\0' ? dirs : "/etc/xdg";
    for (const auto entry : std::views::split(dirs_view, ':'))
    {
        const std::string_view dir(entry.begin(), entry.end());
        if (dir.starts_with('/'))
        {
            config_dirs.emplace_back(dir);
        }
    }

    for (const auto& dir : config_dirs | std::views::reverse)
    {
        auto path = dir / "openxr/1/active_runtime.json";
        if (std::filesystem::exists(path))
        {
            candidates.push_back(std::move(path));
        }
    }
    return candidates;
}

} // namespace

auto Monado::ResultName(MonadoResult result) -> std::string_view
{
    switch (result)
    {
    case MonadoResult::Success: return "Success";
    case MonadoResult::ErrorInvalidVersion: return "ErrorInvalidVersion";
    case MonadoResult::ErrorInvalidValue: return "ErrorInvalidValue";
    case MonadoResult::ErrorConnectingFailed: return "ErrorConnectingFailed";
    case MonadoResult::ErrorOperationFailed: return "ErrorOperationFailed";
    case MonadoResult::ErrorRecenteringNotSupported: return "ErrorRecenteringNotSupported";
    case MonadoResult::ErrorInvalidProperty: return "ErrorInvalidProperty";
    case MonadoResult::ErrorInvalidOperation: return "ErrorInvalidOperation";
    }
    return "Unknown";
}

auto Monado::AutoConnect() -> Result<std::unique_ptr<Monado>>
{
    const auto connect = [](const std::filesystem::path& library) -> Result<std::unique_ptr<Monado>>
    {
        auto monado = Create(library);
        if (!monado)
        {
            return Unexpected(std::string(ResultName(monado.error())));
        }
        return std::move(*monado);
    };

    if (const char* path = std::getenv("LIBMONADO_PATH"))
    {
        if (std::filesystem::is_regular_file(path))
        {
            return connect(path);
        }
        return Unexpected("LIBMONADO_PATH does not point to a valid file");
    }

    for (const auto& candidate : RuntimeJsonCandidates())
    {
        const auto contents = ConfigStore::ReadFile(candidate);
        if (!contents)
        {
            continue;
        }
        const auto json = nlohmann::json::parse(*contents, nullptr, false);
        if (json.is_discarded() || !json.contains("runtime") || !json["runtime"].is_object())
        {
            continue;
        }
        const auto& runtime = json["runtime"];
        if (!runtime.contains("library_path") || !runtime["library_path"].is_string())
        {
            continue;
        }
        if (!runtime.contains("MND_libmonado_path") || !runtime["MND_libmonado_path"].is_string())
        {
            return Unexpected("Couldn't find libmonado path in active runtime json");
        }
        ASSIGN_OR_RETURN(const auto library, ResolveRuntimeLibrary(runtime["MND_libmonado_path"].get<std::string>(), candidate));
        return connect(library);
    }
    return Unexpected("Couldn't find the active runtime json");
}

auto Monado::Create(const std::filesystem::path& library) -> std::expected<std::unique_ptr<Monado>, MonadoResult>
{
    void* handle = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr)
    {
        return std::unexpected(MonadoResult::ErrorConnectingFailed);
    }
    const auto api = LoadApi(handle);
    if (!api)
    {
        dlclose(handle);
        return std::unexpected(MonadoResult::ErrorConnectingFailed);
    }

    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t patch = 0;
    api->get_version(&major, &minor, &patch);
    if (major != 1 || minor < 3)
    {
        dlclose(handle);
        return std::unexpected(MonadoResult::ErrorInvalidVersion);
    }

    void* root = nullptr;
    if (const auto result = ToMonadoResult(api->root_create(&root)); !result)
    {
        dlclose(handle);
        return std::unexpected(result.error());
    }
    return std::unique_ptr<Monado>(new Monado(handle, *api, root));
}

Monado::Monado(void* library, const MonadoApi& api, void* root) : m_library(library), m_api(api), m_root(root)
{
}

Monado::~Monado()
{
    m_api.root_destroy(&m_root);
    dlclose(m_library);
}

auto Monado::Clients() -> std::expected<std::vector<uint32_t>, MonadoResult>
{
    if (const auto result = ToMonadoResult(m_api.update_client_list(m_root)); !result)
    {
        return std::unexpected(result.error());
    }
    uint32_t count = 0;
    if (const auto result = ToMonadoResult(m_api.get_number_clients(m_root, &count)); !result)
    {
        return std::unexpected(result.error());
    }
    std::vector<uint32_t> ids;
    ids.reserve(count);
    for (uint32_t index = 0; index < count; ++index)
    {
        uint32_t id = 0;
        if (const auto result = ToMonadoResult(m_api.get_client_id_at_index(m_root, index, &id)); !result)
        {
            return std::unexpected(result.error());
        }
        ids.push_back(id);
    }
    return ids;
}

auto Monado::ClientName(uint32_t id) -> std::expected<std::string, MonadoResult>
{
    const char* name = nullptr;
    if (const auto result = ToMonadoResult(m_api.get_client_name(m_root, id, &name)); !result)
    {
        return std::unexpected(result.error());
    }
    return std::string(name);
}

auto Monado::ClientState(uint32_t id) -> std::expected<uint32_t, MonadoResult>
{
    uint32_t state = 0;
    if (const auto result = ToMonadoResult(m_api.get_client_state(m_root, id, &state)); !result)
    {
        return std::unexpected(result.error());
    }
    return state;
}

auto Monado::SetClientIoActive(uint32_t id, bool active) -> std::expected<void, MonadoResult>
{
    const auto state = ClientState(id);
    if (!state)
    {
        return std::unexpected(state.error());
    }
    if (((*state & kMonadoClientIoActive) != 0) != active)
    {
        return ToMonadoResult(m_api.toggle_client_io_active(m_root, id));
    }
    return {};
}

auto Monado::DeviceInfo(uint32_t index) -> std::expected<MonadoDevice, MonadoResult>
{
    MonadoDevice device{.index = index};
    const char* name = nullptr;
    if (const auto result = ToMonadoResult(m_api.get_device_info(m_root, index, &device.name_id, &name)); !result)
    {
        return std::unexpected(result.error());
    }
    device.name = name != nullptr ? name : "";
    return device;
}

auto Monado::DeviceFromRole(MonadoDeviceRole role) -> std::expected<MonadoDevice, MonadoResult>
{
    int32_t index = -1;
    if (const auto result = ToMonadoResult(m_api.get_device_from_role(m_root, RoleName(role), &index)); !result)
    {
        return std::unexpected(result.error());
    }
    if (index == -1)
    {
        return std::unexpected(MonadoResult::ErrorInvalidValue);
    }
    return DeviceInfo(static_cast<uint32_t>(index));
}

auto Monado::Devices() -> std::expected<std::vector<MonadoDevice>, MonadoResult>
{
    uint32_t count = 0;
    if (const auto result = ToMonadoResult(m_api.get_device_count(m_root, &count)); !result)
    {
        return std::unexpected(result.error());
    }
    std::vector<MonadoDevice> devices;
    devices.reserve(count);
    for (uint32_t index = 0; index < count; ++index)
    {
        auto device = DeviceInfo(index);
        if (!device)
        {
            return std::unexpected(device.error());
        }
        devices.push_back(std::move(*device));
    }
    return devices;
}

auto Monado::BatteryStatus(const MonadoDevice& device) -> std::expected<MonadoBatteryStatus, MonadoResult>
{
    MonadoBatteryStatus status;
    if (const auto result = ToMonadoResult(
            m_api.get_device_battery_status(m_root, device.index, &status.present, &status.charging, &status.charge)
        );
        !result)
    {
        return std::unexpected(result.error());
    }
    return status;
}

auto Monado::ReferenceSpaceOffset(MonadoReferenceSpace space) -> std::expected<MonadoPose, MonadoResult>
{
    MonadoRawPose raw{};
    if (const auto result = ToMonadoResult(m_api.get_reference_space_offset(m_root, static_cast<int32_t>(space), &raw)); !result)
    {
        return std::unexpected(result.error());
    }
    return MonadoPose{
        .orientation = Eigen::Quaternionf(raw.orientation[3], raw.orientation[0], raw.orientation[1], raw.orientation[2]),
        .position = Eigen::Vector3f(raw.position[0], raw.position[1], raw.position[2]),
    };
}

auto Monado::SetReferenceSpaceOffset(MonadoReferenceSpace space, const MonadoPose& pose) -> std::expected<void, MonadoResult>
{
    const MonadoRawPose raw{
        .orientation = {pose.orientation.x(), pose.orientation.y(), pose.orientation.z(), pose.orientation.w()},
        .position = {pose.position.x(), pose.position.y(), pose.position.z()},
    };
    return ToMonadoResult(m_api.set_reference_space_offset(m_root, static_cast<int32_t>(space), &raw));
}
