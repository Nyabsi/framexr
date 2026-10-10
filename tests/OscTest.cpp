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

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "Osc.hpp"

namespace {

auto ExpectedPacket(std::string_view address, std::string_view tags, const std::vector<unsigned char>& arguments)
    -> std::vector<unsigned char> {
    std::vector<unsigned char> packet(address.begin(), address.end());
    packet.resize(packet.size() + 4 - address.size() % 4, 0);
    const auto tag_start = packet.size();
    packet.insert(packet.end(), tags.begin(), tags.end());
    packet.resize(tag_start + tags.size() + 4 - tags.size() % 4, 0);
    packet.insert(packet.end(), arguments.begin(), arguments.end());
    return packet;
}

class OscWire : public testing::Test {
  protected:
    auto SetUp() -> void override {
        receiver_ = socket(AF_INET, SOCK_DGRAM, 0);
        ASSERT_GE(receiver_, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t length = sizeof(address);
        ASSERT_EQ(bind(receiver_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0);
        getsockname(receiver_, reinterpret_cast<sockaddr*>(&address), &length);
        const timeval timeout{.tv_sec = 2, .tv_usec = 0};
        setsockopt(receiver_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        auto sender = OscSender::Create(ntohs(address.sin_port));
        ASSERT_TRUE(sender.has_value());
        sender_ = std::move(*sender);
    }

    auto TearDown() -> void override { close(receiver_); }

    [[nodiscard]] auto Receive() const -> std::vector<unsigned char> {
        std::array<unsigned char, 512> buffer{};
        const auto received = recv(receiver_, buffer.data(), buffer.size(), 0);
        return received > 0 ? std::vector<unsigned char>(buffer.begin(), buffer.begin() + received) : std::vector<unsigned char>{};
    }

    int receiver_ = -1;
    std::unique_ptr<OscSender> sender_;
};

} // namespace

TEST_F(OscWire, BoolArgumentHasNoPayload) {
    ASSERT_TRUE(sender_->SendMessage("/avatar/parameters/isOverlayOpen", {true}).has_value());
    EXPECT_EQ(Receive(), ExpectedPacket("/avatar/parameters/isOverlayOpen", ",T", {}));
}

TEST_F(OscWire, IntArgumentIsBigEndian) {
    ASSERT_TRUE(sender_->SendMessage("/avatar/parameters/openOverlayCount", {int32_t{5}}).has_value());
    EXPECT_EQ(Receive(), ExpectedPacket("/avatar/parameters/openOverlayCount", ",i", {0, 0, 0, 5}));
}

TEST_F(OscWire, MixedArgumentsArePaddedPerSpec) {
    ASSERT_TRUE(sender_->SendMessage("/a", {0.5f, std::string("xyz"), false}).has_value());
    EXPECT_EQ(Receive(), ExpectedPacket("/a", ",fsF", {0x3f, 0, 0, 0, 'x', 'y', 'z', 0}));
}
