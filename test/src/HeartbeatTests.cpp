/**
 * @file HeartbeatTests.cpp
 *
 * This module contains unit tests of the Discord::Gateway class
 * in sending out and responding to heartbeat and related messages.
 *
 * © 2020 by Richard Walters
 */

#include "Common.hpp"

#include <gtest/gtest.h>
#include <Json/Value.hpp>
#include <string>
#include <vector>

/**
 * This is the test fixture for these tests, providing common
 * setup and teardown for each test.
 */
struct HeartbeatTests
    : public CommonTextFixture
{
};

TEST_F(HeartbeatTests, Heartbeat_Sent_After_Hello_Received) {
    // Arrange
    ASSERT_TRUE(Connect());
    ASSERT_FALSE(webSocket->onText == nullptr);

    // Act
    SendHello();

    // Assert
    EXPECT_EQ(
        std::vector< std::string >({
            Json::Object({
                {"op", 1},
                {"d", nullptr},
            }).ToEncoding(),
        }),
        webSocket->textSent
    );
}

TEST_F(HeartbeatTests, Heartbeat_Sent_After_Heartbeat_Received) {
    // Arrange
    ASSERT_TRUE(Connect());
    ASSERT_FALSE(webSocket->onText == nullptr);

    // Act
    webSocket->onText(
        Json::Object({
            {"op", 1},
            {"d", nullptr},
        }).ToEncoding()
    );

    // Assert
    EXPECT_EQ(
        std::vector< std::string >({
            Json::Object({
                {"op", 1},
                {"d", nullptr},
            }).ToEncoding(),
        }),
        webSocket->textSent
    );
}

TEST_F(HeartbeatTests, Heartbeat_Sent_After_Heartbeat_Interval) {
    // Arrange
    ASSERT_TRUE(Connect());
    ASSERT_FALSE(webSocket->onText == nullptr);

    // Act
    SendHello();
    webSocket->textSent.clear();
    timeKeeper->currentTime += (double)(heartbeatIntervalMilliseconds + 1) / 1000.0;
    EXPECT_TRUE(webSocket->AwaitTexts(1));

    // Assert
    EXPECT_EQ(
        std::vector< std::string >({
            Json::Object({
                {"op", 1},
                {"d", nullptr},
            }).ToEncoding(),
        }),
        webSocket->textSent
    );
}
