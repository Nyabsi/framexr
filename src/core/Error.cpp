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

#include "Error.hpp"

#include <cstdlib>
#include <format>
#include <utility>

#include "Logger.hpp"

Error::Error(std::string message) : message_(std::move(message)) {
}

auto Error::WithContext(std::string_view context) const -> Error {
    return Error(std::format("{}: {}", context, message_));
}

auto Unexpected(std::string message) -> std::unexpected<Error> {
    return std::unexpected<Error>(std::in_place, std::move(message));
}

auto FatalError(std::string_view message) -> void {
    LOG_ERROR("Fatal error: {}", message);
    std::abort();
}
