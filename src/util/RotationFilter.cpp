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

#include "RotationFilter.hpp"

#include <algorithm>
#include <cmath>

#include "OneEuroFilter.hpp"

RotationFilter::RotationFilter(OneEuroSettings settings) : m_settings(settings)
{
}

auto RotationFilter::Reset() -> void
{
    m_value.reset();
    m_speed = 0.0f;
}

auto RotationFilter::Filter(const Eigen::Quaternionf& value, float delta_seconds) -> Eigen::Quaternionf
{
    if (!m_value)
    {
        m_value = value.normalized();
        return *m_value;
    }
    const float delta = std::max(delta_seconds, OneEuroFilter::kMinDeltaSeconds);
    const float raw_speed = m_value->angularDistance(value) / delta;
    m_speed += OneEuroFilter::Alpha(m_settings.derivative_cutoff, delta) * (raw_speed - m_speed);
    const float cutoff = std::fma(m_settings.beta, m_speed, m_settings.min_cutoff);
    m_value = m_value->slerp(OneEuroFilter::Alpha(cutoff, delta), value).normalized();
    return *m_value;
}
