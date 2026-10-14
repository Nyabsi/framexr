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

#include "LayoutInputMethod.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

#include "Logger.hpp"

namespace {

constexpr auto kLanguages = std::to_array<const wchar_t*>({nullptr, L"00000412", L"00000411", L"00000404"});

auto Layouts() -> std::vector<HKL> {
    std::vector<HKL> layouts(static_cast<size_t>(GetKeyboardLayoutList(0, nullptr)));
    GetKeyboardLayoutList(static_cast<int>(layouts.size()), layouts.data());
    return layouts;
}

} // namespace

LayoutInputMethod::~LayoutInputMethod() {
    Select(ImeScript::None);
}

auto LayoutInputMethod::Select(ImeScript script) -> void {
    if (script == m_active) {
        return;
    }
    HWND window = GetForegroundWindow();
    if (m_active == ImeScript::None) {
        m_previous = GetKeyboardLayout(GetWindowThreadProcessId(window, nullptr));
    }
    m_active = script;
    HKL unload = static_cast<HKL>(std::exchange(m_loaded, nullptr));
    HKL target = static_cast<HKL>(m_previous);
    if (script != ImeScript::None) {
        const auto before = Layouts();
        target = LoadKeyboardLayoutW(kLanguages.at(static_cast<size_t>(script)), KLF_NOTELLSHELL);
        m_loaded = std::ranges::find(before, target) == before.end() ? target : nullptr;
    }
    if (target == nullptr) {
        LOG_WARN("The input language for this keyboard is not installed");
    } else {
        PostMessageW(window, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(target));
    }
    if (unload != nullptr && unload != target) {
        UnloadKeyboardLayout(unload);
    }
}
