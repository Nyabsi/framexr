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

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Geometry>

#include "Error.hpp"

enum class MonadoResult : int8_t {
    Success = 0,
    ErrorInvalidVersion = -1,
    ErrorInvalidValue = -2,
    ErrorConnectingFailed = -3,
    ErrorOperationFailed = -4,
    ErrorRecenteringNotSupported = -5,
    ErrorInvalidProperty = -6,
    ErrorInvalidOperation = -7,
};

enum class MonadoReferenceSpace : uint8_t {
    View = 0,
    Local = 1,
    LocalFloor = 2,
    Stage = 3,
    Unbounded = 4,
};

enum class MonadoDeviceRole : uint8_t {
    Head,
    Eyes,
    Left,
    Right,
    Gamepad,
    HandTrackingLeft,
    HandTrackingRight,
};

constexpr uint32_t kMonadoClientSessionVisible = 4;
constexpr uint32_t kMonadoClientIoActive = 32;

struct MonadoPose {
    Eigen::Quaternionf orientation = Eigen::Quaternionf::Identity();
    Eigen::Vector3f position = Eigen::Vector3f::Zero();
};

struct MonadoRawPose {
    float orientation[4];
    float position[3];
};

struct MonadoDevice {
    uint32_t index = 0;
    uint32_t name_id = 0;
    std::string name;
};

struct MonadoBatteryStatus {
    bool present = false;
    bool charging = false;
    float charge = 0.0f;
};

struct MonadoApi {
    void (*get_version)(uint32_t*, uint32_t*, uint32_t*) = nullptr;
    int32_t (*root_create)(void**) = nullptr;
    void (*root_destroy)(void**) = nullptr;
    int32_t (*update_client_list)(void*) = nullptr;
    int32_t (*get_number_clients)(void*, uint32_t*) = nullptr;
    int32_t (*get_client_id_at_index)(void*, uint32_t, uint32_t*) = nullptr;
    int32_t (*get_client_name)(void*, uint32_t, const char**) = nullptr;
    int32_t (*get_client_state)(void*, uint32_t, uint32_t*) = nullptr;
    int32_t (*toggle_client_io_active)(void*, uint32_t) = nullptr;
    int32_t (*get_device_count)(void*, uint32_t*) = nullptr;
    int32_t (*get_device_info)(void*, uint32_t, uint32_t*, const char**) = nullptr;
    int32_t (*get_device_from_role)(void*, const char*, int32_t*) = nullptr;
    int32_t (*get_device_battery_status)(void*, uint32_t, bool*, bool*, float*) = nullptr;
    int32_t (*get_reference_space_offset)(void*, int32_t, MonadoRawPose*) = nullptr;
    int32_t (*set_reference_space_offset)(void*, int32_t, const MonadoRawPose*) = nullptr;
};

class Monado final {
  public:
    static auto AutoConnect() -> Result<std::unique_ptr<Monado>>;
    static auto Create(const std::filesystem::path& library) -> std::expected<std::unique_ptr<Monado>, MonadoResult>;
    static auto ResultName(MonadoResult result) -> std::string_view;

    ~Monado();
    Monado(const Monado&) = delete;
    auto operator=(const Monado&) -> Monado& = delete;

    auto Clients() -> std::expected<std::vector<uint32_t>, MonadoResult>;
    auto ClientName(uint32_t id) -> std::expected<std::string, MonadoResult>;
    auto ClientState(uint32_t id) -> std::expected<uint32_t, MonadoResult>;
    auto SetClientIoActive(uint32_t id, bool active) -> std::expected<void, MonadoResult>;

    auto DeviceFromRole(MonadoDeviceRole role) -> std::expected<MonadoDevice, MonadoResult>;
    auto Devices() -> std::expected<std::vector<MonadoDevice>, MonadoResult>;
    auto BatteryStatus(const MonadoDevice& device) -> std::expected<MonadoBatteryStatus, MonadoResult>;

    auto ReferenceSpaceOffset(MonadoReferenceSpace space) -> std::expected<MonadoPose, MonadoResult>;
    auto SetReferenceSpaceOffset(MonadoReferenceSpace space, const MonadoPose& pose) -> std::expected<void, MonadoResult>;

  private:
    Monado(void* library, const MonadoApi& api, void* root);

    auto DeviceInfo(uint32_t index) -> std::expected<MonadoDevice, MonadoResult>;

    void* m_library;
    MonadoApi m_api;
    void* m_root;
};
