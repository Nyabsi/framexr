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

#include "OpenXrMultiClick.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <utility>

#include "Logger.hpp"
#include "OpenXrInstance.hpp"

namespace
{

constexpr auto kClickTimes = std::to_array<std::chrono::milliseconds>({
    std::chrono::milliseconds(0),
    std::chrono::milliseconds(500),
    std::chrono::milliseconds(750),
});

} // namespace

auto OpenXrMultiClick::Create(
    const OpenXrInstance& instance,
    XrActionSet action_set,
    std::string_view action_name,
    std::string_view side,
    size_t count
) -> Result<OpenXrMultiClick>
{
    auto name = std::format("{}_{}-{}", side, count, action_name);
    const auto float_name = std::format("{}_value", name);
    ASSIGN_OR_RETURN(const auto bool_action, instance.CreateAction(action_set, name, name, XR_ACTION_TYPE_BOOLEAN_INPUT));
    ASSIGN_OR_RETURN(const auto float_action, instance.CreateAction(action_set, float_name, float_name, XR_ACTION_TYPE_FLOAT_INPUT));
    return OpenXrMultiClick(std::move(name), bool_action, float_action, count);
}

OpenXrMultiClick::OpenXrMultiClick(std::string name, XrAction bool_action, XrAction float_action, size_t count)
    : m_name(std::move(name)),
      m_bool_action(bool_action),
      m_float_action(float_action),
      m_count(count),
      m_previous(count, std::chrono::steady_clock::now())
{
}

auto OpenXrMultiClick::Check(const OpenXrState& xr, float threshold) -> Result<bool>
{
    XrActionStateBoolean bool_state{.type = XR_TYPE_ACTION_STATE_BOOLEAN};
    const XrActionStateGetInfo bool_info{.type = XR_TYPE_ACTION_STATE_GET_INFO, .action = m_bool_action};
    RETURN_IF_ERROR(xr.instance->Check(xrGetActionStateBoolean(xr.session, &bool_info, &bool_state)));
    bool state = bool_state.isActive != XR_FALSE && bool_state.currentState != XR_FALSE;

    if (!state)
    {
        XrActionStateFloat float_state{.type = XR_TYPE_ACTION_STATE_FLOAT};
        const XrActionStateGetInfo float_info{.type = XR_TYPE_ACTION_STATE_GET_INFO, .action = m_float_action};
        RETURN_IF_ERROR(xr.instance->Check(xrGetActionStateFloat(xr.session, &float_info, &float_state)));
        state = float_state.isActive != XR_FALSE && float_state.currentState > threshold;
    }

    if (!state)
    {
        m_held_active = false;
        m_held_inactive = false;
        return false;
    }
    if (m_held_active)
    {
        return true;
    }
    if (m_held_inactive)
    {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    const bool passed = std::ranges::all_of(
        m_previous,
        [&](const auto& instant) -> bool { return now - instant < kClickTimes.at(m_count); }
    );

    if (passed)
    {
        LOG_TRACE("{}: passed", m_name);
        m_held_active = true;
        m_held_inactive = false;
        std::ranges::fill(m_previous, now - std::chrono::seconds(10));
    }
    else if (m_count > 0)
    {
        LOG_TRACE("{}: rotate", m_name);
        std::ranges::rotate(m_previous, m_previous.end() - 1);
        m_previous.front() = now;
        m_held_inactive = true;
    }
    return passed;
}
