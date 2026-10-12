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

#include "UInputProvider.hpp"

#include <cerrno>
#include <cstring>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#include "Hid.hpp"
#include "Logger.hpp"
#include "VirtualKey.hpp"

namespace {

constexpr int32_t kMouseExtent = 32768;

auto OpenUinput() -> int {
    return open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
}

auto SetupDevice(int fd, uint16_t product, const char* name) -> bool {
    uinput_setup setup{};
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x4711;
    setup.id.product = product;
    setup.id.version = 5;
    std::strncpy(setup.name, name, UINPUT_MAX_NAME_SIZE - 1);
    return ioctl(fd, UI_DEV_SETUP, &setup) == 0;
}

auto SetupAbsAxis(int fd, uint16_t axis) -> bool {
    uinput_abs_setup abs{};
    abs.code = axis;
    abs.absinfo.minimum = 0;
    abs.absinfo.maximum = kMouseExtent;
    abs.absinfo.resolution = 10;
    return ioctl(fd, UI_ABS_SETUP, &abs) == 0;
}

auto CreateKeyboard(int fd) -> bool {
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) != 0) {
        return false;
    }
    for (const auto key : VirtualKeys::All()) {
        if (ioctl(fd, UI_SET_KEYBIT, static_cast<int>(key) - 8) != 0) {
            return false;
        }
    }
    return SetupDevice(fd, 0x0829, "FrameXR Keyboard") && ioctl(fd, UI_DEV_CREATE) == 0;
}

auto CreateMouse(int fd) -> bool {
    const bool bits = ioctl(fd, UI_SET_EVBIT, EV_ABS) == 0 && ioctl(fd, UI_SET_EVBIT, EV_REL) == 0
                   && ioctl(fd, UI_SET_ABSBIT, ABS_X) == 0 && ioctl(fd, UI_SET_ABSBIT, ABS_Y) == 0
                   && ioctl(fd, UI_SET_RELBIT, REL_WHEEL_HI_RES) == 0 && ioctl(fd, UI_SET_RELBIT, REL_HWHEEL_HI_RES) == 0
                   && ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0;
    if (!bits) {
        return false;
    }
    for (uint16_t button = kMouseLeft; button <= kMouseMiddle; ++button) {
        if (ioctl(fd, UI_SET_KEYBIT, button) != 0) {
            return false;
        }
    }
    return SetupDevice(fd, 0x0830, "FrameXR Mouse") && SetupAbsAxis(fd, ABS_X) && SetupAbsAxis(fd, ABS_Y)
        && ioctl(fd, UI_DEV_CREATE) == 0;
}

} // namespace

auto UInputProvider::Create() -> std::unique_ptr<UInputProvider> {
    const int keyboard_fd = OpenUinput();
    if (keyboard_fd < 0) {
        return nullptr;
    }
    const int mouse_fd = OpenUinput();
    if (mouse_fd < 0) {
        close(keyboard_fd);
        return nullptr;
    }
    if (!CreateKeyboard(keyboard_fd) || !CreateMouse(mouse_fd)) {
        close(keyboard_fd);
        close(mouse_fd);
        return nullptr;
    }
    return std::unique_ptr<UInputProvider>(new UInputProvider(keyboard_fd, mouse_fd));
}

UInputProvider::UInputProvider(int keyboard_fd, int mouse_fd) : m_keyboard_fd(keyboard_fd), m_mouse_fd(mouse_fd) {
}

UInputProvider::~UInputProvider() {
    ioctl(m_keyboard_fd, UI_DEV_DESTROY);
    ioctl(m_mouse_fd, UI_DEV_DESTROY);
    close(m_keyboard_fd);
    close(m_mouse_fd);
}

auto UInputProvider::Write(int fd, std::initializer_list<UInputEvent> events, std::string_view context) const -> void {
    timeval time{};
    gettimeofday(&time, nullptr);
    std::vector<input_event> batch;
    batch.reserve(events.size() + 1);
    for (const auto& event : events) {
        batch.push_back({.time = time, .type = event.type, .code = event.code, .value = event.value});
    }
    batch.push_back({.time = time, .type = EV_SYN, .code = SYN_REPORT, .value = 0});
    if (write(fd, batch.data(), batch.size() * sizeof(input_event)) < 0) {
        const int error = errno;
        LOG_ERROR("{}{} (os error {})", context, std::strerror(error), error);
    }
}

auto UInputProvider::SetModifiers(uint8_t modifiers) -> void {
    const uint8_t changed = m_current_modifiers ^ modifiers;
    for (int bit = 0; bit < 8; ++bit) {
        const auto mask = static_cast<uint8_t>(1U << bit);
        if ((changed & mask) == 0) {
            continue;
        }
        if (const auto key = VirtualKeys::KeyForModifier(mask)) {
            SendKey(*key, (modifiers & mask) != 0);
        }
    }
    m_current_modifiers = modifiers;
}

auto UInputProvider::SendKey(VirtualKey key, bool down) -> void {
    LOG_TRACE("send_key: {} {}", VirtualKeys::Name(key), down);
    Write(m_keyboard_fd, {{EV_KEY, static_cast<uint16_t>(static_cast<uint16_t>(key) - 8), down ? 1 : 0}}, "send_key: ");
}

auto UInputProvider::SetDesktopExtent(const Eigen::Vector2f& extent) -> void {
    m_desktop_extent = extent;
}

auto UInputProvider::SetDesktopOrigin(const Eigen::Vector2f& origin) -> void {
    m_desktop_origin = origin;
}

auto UInputProvider::MouseMove(const Eigen::Vector2f& position) -> void {
    if (!m_pending.position && !m_pending.scroll) {
        m_pending.position = position;
    }
    m_pending.last_requested_position = position;
}

auto UInputProvider::SendButton(uint16_t button, bool down) -> void {
    if (m_pending.buttons.empty()) {
        m_pending.position = m_pending.last_requested_position;
    }
    m_pending.buttons.push_back({.button = button, .down = down});
}

auto UInputProvider::Wheel(int32_t delta_y, int32_t delta_x) -> void {
    if (!m_pending.scroll) {
        m_pending.scroll = Eigen::Vector2i(delta_x, delta_y);
        m_pending.position.reset();
    }
}

auto UInputProvider::Commit() -> void {
    if (const auto position = std::exchange(m_pending.position, std::nullopt)) {
        LOG_TRACE("Mouse move: {} {}", position->x(), position->y());
        const Eigen::Vector2f scaled = (*position - m_desktop_origin)
                                           .cwiseProduct(Eigen::Vector2f::Constant(kMouseExtent).cwiseQuotient(m_desktop_extent));
        Write(
            m_mouse_fd,
            {{EV_ABS, ABS_X, static_cast<int32_t>(scaled.x())}, {EV_ABS, ABS_Y, static_cast<int32_t>(scaled.y())}},
            ""
        );
    }
    for (const auto& button : std::exchange(m_pending.buttons, {})) {
        Write(m_mouse_fd, {{EV_KEY, button.button, button.down ? 1 : 0}}, "send_button: ");
    }
    if (const auto scroll = std::exchange(m_pending.scroll, std::nullopt)) {
        Write(m_mouse_fd, {{EV_REL, REL_WHEEL_HI_RES, scroll->y()}, {EV_REL, REL_HWHEEL_HI_RES, scroll->x()}}, "wheel: ");
    }
}
