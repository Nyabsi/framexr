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

#include "OneEuroFilter.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

OneEuroFilter::OneEuroFilter(OneEuroSettings settings) : m_settings(settings) {
}

auto OneEuroFilter::Alpha(float cutoff, float delta_seconds) -> float {
    const float tau = 1.0f / (2.0f * std::numbers::pi_v<float> * cutoff);
    return 1.0f / (1.0f + tau / std::max(delta_seconds, kMinDeltaSeconds));
}

auto OneEuroFilter::Reset() -> void {
    m_value.reset();
    m_derivative = Eigen::Vector3f::Zero();
}

auto OneEuroFilter::Filter(const Eigen::Vector3f& value, float delta_seconds) -> Eigen::Vector3f {
    if (!m_value) {
        m_value = value;
        return value;
    }
    const float delta = std::max(delta_seconds, kMinDeltaSeconds);
    const Eigen::Vector3f raw_derivative = (value - *m_value) / delta;
    m_derivative += Alpha(m_settings.derivative_cutoff, delta) * (raw_derivative - m_derivative);
    const float cutoff = std::fma(m_settings.beta, m_derivative.norm(), m_settings.min_cutoff);
    *m_value += Alpha(cutoff, delta) * (value - *m_value);
    return *m_value;
}
