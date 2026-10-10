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

#pragma once

#include <expected>
#include <string>
#include <string_view>

class Error {
  public:
    explicit Error(std::string message);

    [[nodiscard]] auto Message() const -> const std::string& { return message_; }
    [[nodiscard]] auto WithContext(std::string_view context) const -> Error;

  private:
    std::string message_;
};

template <typename T = void>
using Result = std::expected<T, Error>;

[[nodiscard]] auto Unexpected(std::string message) -> std::unexpected<Error>;
[[noreturn]] auto FatalError(std::string_view message) -> void;

#define RESULT_CONCAT_IMPL(a, b) a##b
#define RESULT_CONCAT(a, b) RESULT_CONCAT_IMPL(a, b)

#define RETURN_IF_ERROR(expr)                        \
    do {                                             \
        if (auto result_ = (expr); !result_) {       \
            return std::unexpected(result_.error()); \
        }                                            \
    } while (false)

// NOLINTBEGIN(bugprone-macro-parentheses)
#define ASSIGN_OR_RETURN_IMPL(result, lhs, expr) \
    auto result = (expr);                        \
    if (!result) {                               \
        return std::unexpected(result.error());  \
    }                                            \
    lhs = std::move(*result)
// NOLINTEND(bugprone-macro-parentheses)

#define ASSIGN_OR_RETURN(lhs, expr) ASSIGN_OR_RETURN_IMPL(RESULT_CONCAT(result_, __LINE__), lhs, expr)
