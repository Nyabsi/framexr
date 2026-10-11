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
#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "Osc.hpp"
#include "UdpSocket.hpp"

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
        auto receiver = UdpSocket::Bind(UdpAddress::Loopback, 0);
        ASSERT_TRUE(receiver.has_value());
        receiver_ = std::move(*receiver);
        ASSERT_TRUE(receiver_->SetReceiveTimeout(std::chrono::seconds(2)).has_value());
        auto sender = OscSender::Create(receiver_->LocalPort());
        ASSERT_TRUE(sender.has_value());
        sender_ = std::move(*sender);
    }

    [[nodiscard]] auto Receive() const -> std::vector<unsigned char> {
        std::array<std::byte, 512> buffer{};
        const auto received = receiver_->Receive(buffer);
        std::vector<unsigned char> bytes;
        for (const auto byte : std::span(buffer).first(received.value_or(0))) {
            bytes.push_back(std::to_integer<unsigned char>(byte));
        }
        return bytes;
    }

    std::optional<UdpSocket> receiver_;
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
