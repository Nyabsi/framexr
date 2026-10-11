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

#include "Hid.hpp"

#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#include "Logger.hpp"

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

auto HidProvider::Create() -> std::unique_ptr<HidProvider> {
    if (!use_uinput.load()) {
        LOG_INFO("Uinput disabled by user.");
        return std::make_unique<NullHidProvider>();
    }
    if (auto uinput = UInputProvider::Create()) {
        LOG_INFO("Initialized uinput.");
        return uinput;
    }
    LOG_ERROR("Could not create uinput provider. Keyboard/Mouse input will not work!");
    LOG_ERROR("To check if you're in input group, run: id -nG");
    if (const char* user = std::getenv("USER")) {
        LOG_ERROR("To add yourself to the input group, run: sudo usermod -aG input {}", user);
        LOG_ERROR("After adding yourself to the input group, you will need to reboot.");
    }
    return std::make_unique<NullHidProvider>();
}

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

UInputProvider::UInputProvider(int keyboard_fd, int mouse_fd) : keyboard_fd_(keyboard_fd), mouse_fd_(mouse_fd) {
}

UInputProvider::~UInputProvider() {
    ioctl(keyboard_fd_, UI_DEV_DESTROY);
    ioctl(mouse_fd_, UI_DEV_DESTROY);
    close(keyboard_fd_);
    close(mouse_fd_);
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
    const uint8_t changed = current_modifiers_ ^ modifiers;
    for (int bit = 0; bit < 8; ++bit) {
        const auto mask = static_cast<uint8_t>(1U << bit);
        if ((changed & mask) == 0) {
            continue;
        }
        if (const auto key = VirtualKeys::KeyForModifier(mask)) {
            SendKey(*key, (modifiers & mask) != 0);
        }
    }
    current_modifiers_ = modifiers;
}

auto UInputProvider::SendKey(VirtualKey key, bool down) -> void {
    LOG_TRACE("send_key: {} {}", VirtualKeys::Name(key), down);
    Write(keyboard_fd_, {{EV_KEY, static_cast<uint16_t>(static_cast<uint16_t>(key) - 8), down ? 1 : 0}}, "send_key: ");
}

auto UInputProvider::SetDesktopExtent(const Eigen::Vector2f& extent) -> void {
    desktop_extent_ = extent;
}

auto UInputProvider::SetDesktopOrigin(const Eigen::Vector2f& origin) -> void {
    desktop_origin_ = origin;
}

auto UInputProvider::MouseMove(const Eigen::Vector2f& position) -> void {
    if (!pending_.position && !pending_.scroll) {
        pending_.position = position;
    }
    pending_.last_requested_position = position;
}

auto UInputProvider::SendButton(uint16_t button, bool down) -> void {
    if (pending_.buttons.empty()) {
        pending_.position = pending_.last_requested_position;
    }
    pending_.buttons.push_back({.button = button, .down = down});
}

auto UInputProvider::Wheel(int32_t delta_y, int32_t delta_x) -> void {
    if (!pending_.scroll) {
        pending_.scroll = Eigen::Vector2i(delta_x, delta_y);
        pending_.position.reset();
    }
}

auto UInputProvider::Commit() -> void {
    if (const auto position = std::exchange(pending_.position, std::nullopt)) {
        LOG_TRACE("Mouse move: {} {}", position->x(), position->y());
        const Eigen::Vector2f scaled = (*position - desktop_origin_)
                                           .cwiseProduct(Eigen::Vector2f::Constant(kMouseExtent).cwiseQuotient(desktop_extent_));
        Write(mouse_fd_, {{EV_ABS, ABS_X, static_cast<int32_t>(scaled.x())}, {EV_ABS, ABS_Y, static_cast<int32_t>(scaled.y())}}, "");
    }
    for (const auto& button : std::exchange(pending_.buttons, {})) {
        Write(mouse_fd_, {{EV_KEY, button.button, button.down ? 1 : 0}}, "send_button: ");
    }
    if (const auto scroll = std::exchange(pending_.scroll, std::nullopt)) {
        Write(mouse_fd_, {{EV_REL, REL_WHEEL_HI_RES, scroll->y()}, {EV_REL, REL_HWHEEL_HI_RES, scroll->x()}}, "wheel: ");
    }
}
