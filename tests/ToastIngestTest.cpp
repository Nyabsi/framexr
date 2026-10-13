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

#include <cstring>

#include <gtest/gtest.h>

#include "Notifications.hpp"

namespace {

auto SampleToast() -> ToastMessage {
    return {
        .topic = ToastTopic::XsNotification,
        .title = "Title ✓",
        .body = "Body",
        .opacity = 0.75f,
        .timeout = 4.5f,
        .sound = true,
    };
}

} // namespace

TEST(ToastIngest, BinaryFormatRoundTrips) {
    const auto toast = SampleToast();
    const auto encoded = ToastIngest::Encode(toast);
    ASSERT_GE(encoded.size(), 4U);
    EXPECT_EQ(std::memcmp(encoded.data(), "FXRT", 4), 0);

    const auto decoded = ToastIngest::Decode(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->topic, toast.topic);
    EXPECT_EQ(decoded->title, toast.title);
    EXPECT_EQ(decoded->body, toast.body);
    EXPECT_EQ(decoded->opacity, toast.opacity);
    EXPECT_EQ(decoded->timeout, toast.timeout);
    EXPECT_TRUE(decoded->sound);
}

TEST(ToastIngest, RejectsTruncatedMessages) {
    auto encoded = ToastIngest::Encode(SampleToast());
    encoded.pop_back();
    EXPECT_FALSE(ToastIngest::Decode(encoded).has_value());
}

TEST(ToastIngest, XsOverlayNotificationMapsLikeOriginal) {
    const auto toast = ToastIngest::FromXsOverlay(R"({"messageType":1,"title":"Hi","content":"there","volume":0,"timeout":2})");
    ASSERT_TRUE(toast.has_value());
    ASSERT_TRUE(toast->has_value());
    const auto& message = **toast;
    EXPECT_EQ(message.topic, ToastTopic::XsNotification);
    EXPECT_EQ(message.title, "Hi");
    EXPECT_EQ(message.body, "there");
    EXPECT_EQ(message.timeout, 2.0f);
    EXPECT_TRUE(message.sound);
}

TEST(ToastIngest, XsOverlayDefaults) {
    const auto toast = ToastIngest::FromXsOverlay(R"({"messageType":1,"title":"Hi"})");
    ASSERT_TRUE(toast.has_value() && toast->has_value());
    EXPECT_FALSE((*toast)->sound);
    EXPECT_EQ((*toast)->timeout, 5.0f);
    EXPECT_TRUE((*toast)->body.empty());
}

TEST(ToastIngest, XsOverlayIgnoresOtherMessageTypes) {
    const auto toast = ToastIngest::FromXsOverlay(R"({"messageType":2,"title":"Hi"})");
    ASSERT_TRUE(toast.has_value());
    EXPECT_FALSE(toast->has_value());
}

TEST(ToastIngest, XsOverlayRequiresTitle) {
    EXPECT_FALSE(ToastIngest::FromXsOverlay(R"({"messageType":1})").has_value());
}

TEST(ToastIngest, QueueKeepsAtMostTenPendingToasts) {
    ToastIngest ingest;
    for (int i = 0; i < 12; ++i) {
        ingest.Push(SampleToast());
    }
    EXPECT_EQ(ingest.Drain().size(), 10U);
    EXPECT_TRUE(ingest.Drain().empty());
}

TEST(ToastIngest, Utf8ErrorsMatchRustStdStr) {
    EXPECT_EQ(ToastIngest::Utf8Error("plain ascii ✓"), std::nullopt);
    EXPECT_EQ(ToastIngest::Utf8Error("abc\xff"), "Utf8Error { valid_up_to: 3, error_len: Some(1) }");
    EXPECT_EQ(ToastIngest::Utf8Error("ab\xe2\x82"), "Utf8Error { valid_up_to: 2, error_len: None }");
    EXPECT_EQ(ToastIngest::Utf8Error("\xe2\x28\xa1"), "Utf8Error { valid_up_to: 0, error_len: Some(1) }");
    EXPECT_EQ(ToastIngest::Utf8Error("x\xf0\x90\x28"), "Utf8Error { valid_up_to: 1, error_len: Some(2) }");
    EXPECT_EQ(ToastIngest::Utf8Error("\xed\xa0\x80"), "Utf8Error { valid_up_to: 0, error_len: Some(1) }");
    EXPECT_EQ(ToastIngest::Utf8Error("\xc0\x80"), "Utf8Error { valid_up_to: 0, error_len: Some(1) }");
}
