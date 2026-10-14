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

#include "OpenVrMath.hpp"

auto OpenVrMath::ToAffine(const vr::HmdMatrix34_t& matrix) -> Eigen::Affine3f
{
    Eigen::Affine3f affine = Eigen::Affine3f::Identity();
    for (Eigen::Index row = 0; row < 3; ++row)
    {
        for (Eigen::Index column = 0; column < 3; ++column)
        {
            affine.linear()(row, column) = matrix.m[row][column];
        }
        affine.translation()(row) = matrix.m[row][3];
    }
    return affine;
}

auto OpenVrMath::ToHmdMatrix(const Eigen::Affine3f& affine) -> vr::HmdMatrix34_t
{
    vr::HmdMatrix34_t matrix{};
    for (Eigen::Index row = 0; row < 3; ++row)
    {
        for (Eigen::Index column = 0; column < 3; ++column)
        {
            matrix.m[row][column] = affine.linear()(row, column);
        }
        matrix.m[row][3] = affine.translation()(row);
    }
    return matrix;
}
