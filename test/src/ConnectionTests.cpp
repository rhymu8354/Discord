/**
 * @file ConnectionTests.cpp
 *
 * This module contains unit tests of the Discord::Gateway class
 * in establishing and breaking connections to Discord.
 *
 * © 2020 by Richard Walters
 */

#include "Common.hpp"

#include <future>
#include <gtest/gtest.h>
#include <Json/Value.hpp>
#include <string>
#include <vector>

/**
 * This is the test fixture for these tests, providing common
 * setup and teardown for each test.
 */
struct ConnectionTests
    : public CommonTextFixture
{
};

TEST_F(ConnectionTests, First_Connect_Requests_WebSocket_Endpoint) {
    // Arrange
    const std::string userAgent = "DiscordBot";

    // Act
    connected = gateway.Connect(connections, userAgent);

    // Assert
    ASSERT_TRUE(connections->RequireResourceRequests(1));
    auto& requestWithPromise = *connections->resourceRequests[0];
    EXPECT_EQ("GET", requestWithPromise.request.method);
    EXPECT_EQ(
        "https://discordapp.com/api/v6/gateway",
        requestWithPromise.request.uri
    );
    ExpectHeaders(
        std::vector< Discord::Connections::Header >({
            {"User-Agent", userAgent},
        }),
        requestWithPromise.request.headers
    );
}

TEST_F(ConnectionTests, Connect_Still_Connecting) {
    // Arrange
    const std::string userAgent = "DiscordBot";
    connected = gateway.Connect(connections, userAgent);

    // Act
    auto secondConnectResult = gateway.Connect(connections, userAgent);

    // Assert
    EXPECT_FALSE(secondConnectResult.get());
}

TEST_F(ConnectionTests, Connect_Fails_For_Non_OK_WebSocket_Endpoint_Response) {
    // Arrange
    std::future< bool > connected;

    // Act
    connected = gateway.Connect(connections, "DiscordBot");
    ASSERT_TRUE(connections->RequireResourceRequests(1));
    connections->RespondToResourceRequest(0, {404});

    // Assert
    EXPECT_FALSE(connected.get());
}

TEST_F(ConnectionTests, Connect_Fails_For_Bad_WebSocket_Endpoint_Responses) {
    // Arrange
    std::future< bool > connected;

    // Act
    struct ConnectResultInfo {
        std::string responseBody;
        bool connectResult;
    };
    static const std::vector< std::string > badWebSocketEndpointResponses{
        "This is \" bad JSON",
        "foobar",
        Json::Object({
            {"foo", "wss://gateway.discord.gg"},
        }).ToEncoding()
    };
    std::vector< ConnectResultInfo > connectResults;
    for (const auto& responseBody: badWebSocketEndpointResponses) {
        ASSERT_TRUE(
            ConnectExpectingWebSocketEndpointRequestWithResponse(responseBody, connected)
        ) << responseBody;
        ConnectResultInfo connectResultsInfo;
        connectResultsInfo.responseBody = responseBody;
        connectResultsInfo.connectResult = connected.get();
        connectResults.push_back(std::move(connectResultsInfo));
    }

    // Assert
    for (const auto connectResultInfo: connectResults) {
        EXPECT_FALSE(connectResultInfo.connectResult) << connectResultInfo.responseBody;
    }
}

TEST_F(ConnectionTests, Connect_Fails_When_Disconnect_During_WebSocket_Endpoint_Request) {
    // Arrange
    const std::string userAgent = "DiscordBot";
    connected = gateway.Connect(connections, userAgent);

    // Act
    ASSERT_TRUE(connections->RequireResourceRequests(1));
    gateway.Disconnect();
    ASSERT_EQ(
        std::future_status::ready,
        connections->resourceRequests[0]->canceled.get_future().wait_for(
            std::chrono::milliseconds(100)
        )
    );
    const auto connectedReady = (
        connected.wait_for(
            std::chrono::milliseconds(100)
        )
        == std::future_status::ready
    );

    // Assert
    ASSERT_TRUE(connectedReady);
    EXPECT_FALSE(connected.get());
}

TEST_F(ConnectionTests, Connect_Fails_When_Disconnect_Before_WebSocket_Endpoint_Request) {
    // Arrange
    const std::string userAgent = "DiscordBot";
    std::promise< void > proceedWithConnect;
    gateway.WaitBeforeConnect(proceedWithConnect.get_future());
    connected = gateway.Connect(connections, userAgent);

    // Act
    gateway.Disconnect();
    proceedWithConnect.set_value();
    EXPECT_FALSE(connections->RequireResourceRequests(1));
    const auto connectedReady = (
        connected.wait_for(
            std::chrono::milliseconds(100)
        )
        == std::future_status::ready
    );

    // Assert
    ASSERT_TRUE(connectedReady);
    EXPECT_FALSE(connected.get());
}

TEST_F(ConnectionTests, First_Connect_Requests_WebSocket_After_Receiving_WebSocket_Endpoint) {
    // Arrange
    const std::string webSocketEndpoint = "wss://gateway.discord.gg";

    // Act
    const auto webSocketEndpointRequested = ConnectExpectingWebSocketEndpointRequestWithResponse(
        Json::Object({
            {"url", webSocketEndpoint},
        }).ToEncoding(),
        connected
    );

    // Assert
    ASSERT_TRUE(webSocketEndpointRequested);
    ASSERT_TRUE(connections->RequireWebSocketRequests(1));
    auto& requestWithPromise = *connections->webSocketRequests[0];
    EXPECT_EQ(
        webSocketEndpoint + "/?v=6&encoding=json",
        requestWithPromise.request.uri
    );
}

TEST_F(ConnectionTests, Connect_Completes_Successfully_Once_WebSocket_Obtained) {
    // Arrange

    // Act
    const auto connected = Connect();

    // Assert
    EXPECT_TRUE(connected);
}

TEST_F(ConnectionTests, Connect_Already_Connected) {
    // Arrange
    ASSERT_TRUE(Connect());

    // Act
    connected = gateway.Connect(connections, "DiscordBot");
    const auto connectedReady = (
        connected.wait_for(
            std::chrono::milliseconds(100)
        )
        == std::future_status::ready
    );

    // Assert
    ASSERT_TRUE(connectedReady);
    EXPECT_FALSE(connected.get());
}

TEST_F(ConnectionTests, Disconnect) {
    // Arrange
    ASSERT_TRUE(Connect());

    // Act
    gateway.Disconnect();

    // Assert
    EXPECT_TRUE(webSocket->closed);
}

TEST_F(ConnectionTests, Second_Connect_Does_Not_Request_WebSocket_Endpoint_At_First) {
    // Arrange
    ASSERT_TRUE(Connect());
    gateway.Disconnect();

    // Act
    connected = gateway.Connect(connections, "DiscordBot");

    // Assert
    EXPECT_FALSE(connections->RequireResourceRequests(2));
}

TEST_F(ConnectionTests, Second_Connect_Requests_WebSocket) {
    // Arrange
    const std::string webSocketEndpoint = "wss://gateway.discord.gg";
    ASSERT_TRUE(Connect(webSocketEndpoint));
    gateway.Disconnect();

    // Act
    connected = gateway.Connect(connections, "DiscordBot");

    // Assert
    EXPECT_TRUE(connections->RequireWebSocketRequests(2));
    auto& requestWithPromise = *connections->webSocketRequests[1];
    EXPECT_EQ(
        webSocketEndpoint + "/?v=6&encoding=json",
        requestWithPromise.request.uri
    );
}

TEST_F(ConnectionTests, Second_Connect_Requests_WebSocket_Endpoint_If_WebSocket_Open_Fails) {
    // Arrange
    const std::string userAgent = "DiscordBot";
    ASSERT_TRUE(Connect());
    gateway.Disconnect();
    connected = gateway.Connect(connections, userAgent);

    // Act
    ASSERT_TRUE(connections->RequireWebSocketRequests(2));
    connections->RespondToWebSocketRequest(1, nullptr);
    const auto resourceRequested = connections->RequireResourceRequests(2);

    // Assert
    EXPECT_TRUE(resourceRequested);
    auto& requestWithPromise = *connections->resourceRequests[1];
    EXPECT_EQ("GET", requestWithPromise.request.method);
    EXPECT_EQ(
        "https://discordapp.com/api/v6/gateway",
        requestWithPromise.request.uri
    );
    ExpectHeaders(
        std::vector< Discord::Connections::Header >({
            {"User-Agent", userAgent},
        }),
        requestWithPromise.request.headers
    );
}

TEST_F(ConnectionTests, Second_Connect_Second_WebSocket_Attempt_When_First_WebSocket_Open_Fails) {
    // Arrange
    ASSERT_TRUE(Connect());
    gateway.Disconnect();
    connected = gateway.Connect(connections, "DiscordBot");

    // Act
    ASSERT_TRUE(connections->RequireWebSocketRequests(2));
    connections->RespondToWebSocketRequest(1, nullptr);
    ASSERT_TRUE(connections->RequireResourceRequests(2));
    connections->RespondToResourceRequest(1, {
        200,
        {},
        Json::Object({
            {"url", "wss://gateway.discord.gg"},
        }).ToEncoding(),
    });
    const auto secondWebSocketRequested = connections->RequireWebSocketRequests(3);

    // Assert
    EXPECT_TRUE(secondWebSocketRequested);
}

TEST_F(ConnectionTests, Second_Connect_Succeeds_After_Second_WebSocket_Connected_When_First_WebSocket_Open_Fails) {
    // Arrange
    ASSERT_TRUE(Connect());
    gateway.Disconnect();
    connected = gateway.Connect(connections, "DiscordBot");

    // Act
    ASSERT_TRUE(connections->RequireWebSocketRequests(2));
    connections->RespondToWebSocketRequest(1, nullptr);
    ASSERT_TRUE(connections->RequireResourceRequests(2));
    connections->RespondToResourceRequest(1, {
        200,
        {},
        Json::Object({
            {"url", "wss://gateway.discord.gg"},
        }).ToEncoding(),
    });
    ASSERT_TRUE(connections->RequireWebSocketRequests(3));
    connections->RespondToWebSocketRequest(2, webSocket);
    const auto connectedReady = (
        connected.wait_for(
            std::chrono::milliseconds(100)
        )
        == std::future_status::ready
    );

    // Assert
    EXPECT_TRUE(connectedReady);
    EXPECT_TRUE(connected.get());
}

TEST_F(ConnectionTests, Second_Connect_Fails_After_Failed_Second_WebSocket_Attempt_When_First_WebSocket_Open_Fails) {
    // Arrange
    ASSERT_TRUE(Connect());
    gateway.Disconnect();
    connected = gateway.Connect(connections, "DiscordBot");

    // Act
    ASSERT_TRUE(connections->RequireWebSocketRequests(2));
    connections->RespondToWebSocketRequest(1, nullptr);
    ASSERT_TRUE(connections->RequireResourceRequests(2));
    connections->RespondToResourceRequest(1, {
        200,
        {},
        Json::Object({
            {"url", "wss://gateway.discord.gg"},
        }).ToEncoding(),
    });
    ASSERT_TRUE(connections->RequireWebSocketRequests(3));
    connections->RespondToWebSocketRequest(2, nullptr);
    const auto connectedReady = (
        connected.wait_for(
            std::chrono::milliseconds(100)
        )
        == std::future_status::ready
    );

    // Assert
    EXPECT_TRUE(connectedReady);
    EXPECT_FALSE(connected.get());
}

TEST_F(ConnectionTests, Close_Callback_When_WebSocket_Closed_After_Callback_Registered) {
    // Arrange
    ASSERT_TRUE(Connect());

    // Act
    bool closed = false;
    gateway.RegisterCloseCallback(
        [&]{
            closed = true;
        }
    );
    webSocket->RemoteClose();

    // Assert
    EXPECT_TRUE(closed);
}

TEST_F(ConnectionTests, Close_Callback_When_WebSocket_Closed_Before_Callback_Registered) {
    // Arrange
    ASSERT_TRUE(Connect());

    // Act
    webSocket->RemoteClose();
    bool closed = false;
    gateway.RegisterCloseCallback(
        [&]{
            closed = true;
        }
    );

    // Assert
    EXPECT_TRUE(closed);
}
