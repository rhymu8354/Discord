/**
 * @file GatewayTests.cpp
 *
 * This module contains the unit tests of the Discord::Gateway class.
 *
 * © 2020 by Richard Walters
 */

#include <gtest/gtest.h>
#include <Discord/Gateway.hpp>
#include <Json/Value.hpp>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace {

    /**
     * This is a fake WebSocket dependency which is used to test the Gateway
     * class.
     */
    struct MockWebSocket
        : public Discord::WebSocket
    {
        // Properties

        bool closed = false;
        CloseCallback onClose;

        // Methods

        void RemoteClose() {
            if (onClose != nullptr) {
                onClose();
            }
        }

        // Discord::WebSocket

        virtual void Binary(std::string&& message) override {
        }

        virtual void Close() override {
            closed = true;
        }

        virtual void Text(std::string&& message) override {
        }

        virtual void RegisterBinaryCallback(ReceiveCallback&& onBinary) override {
        }

        virtual void RegisterCloseCallback(CloseCallback&& onClose) override {
            this->onClose = std::move(onClose);
        }

        virtual void RegisterTextCallback(ReceiveCallback&& onText) override {
        }
    };

    /**
     * This is a fake connections dependency which is used to test the Gateway
     * class.
     */
    struct MockConnections
        : public Discord::Connections
    {
        // Types

        struct ResourceRequestsWait {
            size_t numRequests = 0;
            std::promise< void > haveRequiredRequests;
        };

        struct ResourceRequestWithPromise {
            ResourceRequest request;
            std::promise< Response > responsePromise;
            bool responded = false;
        };

        struct WebSocketRequestsWait {
            size_t numRequests = 0;
            std::promise< void > haveRequiredRequests;
        };

        struct WebSocketRequestWithPromise {
            WebSocketRequest request;
            std::promise< std::shared_ptr< Discord::WebSocket > > webSocketPromise;
            bool responded = false;
        };

        // Properties

        std::mutex mutex;
        bool tornDown = false;
        std::vector< ResourceRequestWithPromise > resourceRequests;
        ResourceRequestsWait resourceRequestsWait;
        std::vector< WebSocketRequestWithPromise > webSocketRequests;
        WebSocketRequestsWait webSocketRequestsWait;

        // Methods

        bool ExpectSoon(
            std::promise< void >& asyncResult,
            std::unique_lock< decltype(mutex) >& lock
        ) {
            lock.unlock();
            const auto result = asyncResult.get_future().wait_for(
                std::chrono::milliseconds(100)
            );
            lock.lock();
            return(result == std::future_status::ready);
        }

        bool RequireResourceRequests(size_t numRequests) {
            std::unique_lock< decltype(mutex) > lock(mutex);
            if (resourceRequests.size() >= numRequests) {
                return true;
            }
            resourceRequestsWait.numRequests = numRequests;
            resourceRequestsWait.haveRequiredRequests = decltype(resourceRequestsWait.haveRequiredRequests)();
            return ExpectSoon(resourceRequestsWait.haveRequiredRequests, lock);
        }

        bool RequireWebSocketRequests(size_t numRequests) {
            std::unique_lock< decltype(mutex) > lock(mutex);
            if (webSocketRequests.size() >= numRequests) {
                return true;
            }
            webSocketRequestsWait.numRequests = numRequests;
            webSocketRequestsWait.haveRequiredRequests = decltype(webSocketRequestsWait.haveRequiredRequests)();
            return ExpectSoon(webSocketRequestsWait.haveRequiredRequests, lock);
        }

        void RespondToResourceRequest(
            size_t requestIndex,
            Response&& response
        ) {
            resourceRequests[requestIndex].responsePromise.set_value(std::move(response));
            resourceRequests[requestIndex].responded = true;
        }

        void RespondToWebSocketRequest(
            size_t requestIndex,
            std::shared_ptr< Discord::WebSocket > webSocket
        ) {
            webSocketRequests[requestIndex].webSocketPromise.set_value(std::move(webSocket));
            webSocketRequests[requestIndex].responded = true;
        }

        void TearDown() {
            std::lock_guard< decltype(mutex) > lock(mutex);
            tornDown = true;
            for (auto& request: resourceRequests) {
                if (!request.responded) {
                    request.responsePromise.set_value({500});
                }
            }
            for (auto& request: webSocketRequests) {
                if (!request.responded) {
                    request.webSocketPromise.set_value(nullptr);
                }
            }
        }

        // Discord::Connections

        virtual std::future< Response > QueueResourceRequest(
            const ResourceRequest& request
        ) override {
            std::lock_guard< decltype(mutex) > lock(mutex);
            ResourceRequestWithPromise requestWithPromise;
            requestWithPromise.request = request;
            auto future = requestWithPromise.responsePromise.get_future();
            if (tornDown) {
                requestWithPromise.responsePromise.set_value({500});
            } else {
                resourceRequests.push_back(std::move(requestWithPromise));
                if (
                    (resourceRequestsWait.numRequests > 0)
                    && (resourceRequests.size() == resourceRequestsWait.numRequests)
                ) {
                    resourceRequestsWait.haveRequiredRequests.set_value();
                }
            }
            return future;
        }

        virtual std::future< std::shared_ptr< Discord::WebSocket > > QueueWebSocketRequest(
            const WebSocketRequest& request
        ) override {
            std::lock_guard< decltype(mutex) > lock(mutex);
            WebSocketRequestWithPromise requestWithPromise;
            requestWithPromise.request = request;
            auto future = requestWithPromise.webSocketPromise.get_future();
            if (tornDown) {
                requestWithPromise.webSocketPromise.set_value(nullptr);
            } else {
                webSocketRequests.push_back(std::move(requestWithPromise));
                if (
                    (webSocketRequestsWait.numRequests > 0)
                    && (webSocketRequests.size() == webSocketRequestsWait.numRequests)
                ) {
                    webSocketRequestsWait.haveRequiredRequests.set_value();
                }
            }
            return future;
        }
    };

    /**
     * This is a fake time-keeper which is used to test the Gateway class.
     */
    struct MockTimeKeeper
        : public Discord::TimeKeeper
    {
        // Properties

        double currentTime = 0.0;

        // Methods

        // Discord::TimeKeeper

        virtual double GetCurrentTime() override {
            return currentTime;
        }
    };

}

/**
 * This is the test fixture for these tests, providing common
 * setup and teardown for each test.
 */
struct GatewayTests
    : public ::testing::Test
{
    // Properties

    /**
     * This is the unit under test.
     */
    std::future< bool > connected;
    std::shared_ptr< MockConnections > connections = std::make_shared< MockConnections >();
    Discord::Gateway gateway;
    std::shared_ptr< MockWebSocket > webSocket = std::make_shared< MockWebSocket >();

    // Methods

    void ExpectHeaders(
        const std::vector< Discord::Connections::Header >& expected,
        const std::vector< Discord::Connections::Header >& actual
    ) {
        std::unordered_map< std::string, std::string > notFound;
        for (const auto& header: expected) {
            notFound[header.key] = header.value;
        }
        for (const auto& header: actual) {
            const auto notFoundEntry = notFound.find(header.key);
            EXPECT_FALSE(notFoundEntry == notFound.end()) << header.key;
            if (notFoundEntry != notFound.end()) {
                EXPECT_EQ(notFoundEntry->second, header.value) << header.key;
                (void)notFound.erase(notFoundEntry);
            }
        }
        EXPECT_EQ(
            (std::unordered_map< std::string, std::string >({})),
            notFound
        );
    }

    bool ConnectExpectingWebSocketEndpointRequestWithResponse(
        const std::string& webSocketEndpointRequestResponse,
        std::future< bool >& connected
    ){
        const auto nextResourceRequest = connections->resourceRequests.size();
        connected = gateway.Connect(connections, "DiscordBot");
        const auto resourceRequested = connections->RequireResourceRequests(nextResourceRequest + 1);
        if (!resourceRequested) {
            return false;
        }
        connections->RespondToResourceRequest(nextResourceRequest, {
            200,
            {},
            webSocketEndpointRequestResponse
        });
        return true;
    }

    bool Connect() {
        const auto nextWebSocketRequest = connections->webSocketRequests.size();
        const auto resourceRequested = ConnectExpectingWebSocketEndpointRequestWithResponse(
            Json::Object({
                {"url", "wss://gateway.discord.gg"},
            }).ToEncoding(),
            connected
        );
        EXPECT_TRUE(resourceRequested);
        if (!resourceRequested) {
            return false;
        }
        const auto gotWebSocketRequest = connections->RequireWebSocketRequests(nextWebSocketRequest + 1);
        EXPECT_TRUE(gotWebSocketRequest);
        if (!gotWebSocketRequest) {
            return false;
        }
        connections->RespondToWebSocketRequest(nextWebSocketRequest, webSocket);
        const auto connectedReady = (
            connected.wait_for(
                std::chrono::milliseconds(100)
            )
            == std::future_status::ready
        );
        EXPECT_TRUE(connectedReady);
        if (!connectedReady) {
            return false;
        }
        return connected.get();
    }

    // ::testing::Test

    virtual void SetUp() override {
    }

    virtual void TearDown() override {
        connections->TearDown();
    }
};

TEST_F(GatewayTests, First_Connect_Requests_WebSocket_Endpoint) {
    // Arrange
    const std::string userAgent = "DiscordBot";

    // Act
    connected = gateway.Connect(connections, userAgent);

    // Assert
    ASSERT_TRUE(connections->RequireResourceRequests(1));
    auto& requestWithPromise = connections->resourceRequests[0];
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

TEST_F(GatewayTests, Connect_Still_Connecting) {
    // Arrange
    const std::string userAgent = "DiscordBot";
    connected = gateway.Connect(connections, userAgent);

    // Act
    auto secondConnectResult = gateway.Connect(connections, userAgent);

    // Assert
    EXPECT_FALSE(secondConnectResult.get());
}

TEST_F(GatewayTests, Connect_Fails_For_Non_OK_WebSocket_Endpoint_Response) {
    // Arrange
    std::future< bool > connected;

    // Act
    connected = gateway.Connect(connections, "DiscordBot");
    ASSERT_TRUE(connections->RequireResourceRequests(1));
    connections->RespondToResourceRequest(0, {404});

    // Assert
    EXPECT_FALSE(connected.get());
}

TEST_F(GatewayTests, Connect_Fails_For_Bad_WebSocket_Endpoint_Responses) {
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

TEST_F(GatewayTests, First_Connect_Requests_WebSocket_After_Receiving_WebSocket_Endpoint) {
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
    auto& requestWithPromise = connections->webSocketRequests[0];
    EXPECT_EQ(
        webSocketEndpoint + "/?v=6&encoding=json",
        requestWithPromise.request.uri
    );
}

TEST_F(GatewayTests, Connect_Completes_Successfully_Once_WebSocket_Obtained) {
    // Arrange

    // Act
    const auto connected = Connect();

    // Assert
    EXPECT_TRUE(connected);
}

TEST_F(GatewayTests, Connect_Already_Connected) {
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

TEST_F(GatewayTests, Disconnect) {
    // Arrange
    ASSERT_TRUE(Connect());

    // Act
    gateway.Disconnect();

    // Assert
    EXPECT_TRUE(webSocket->closed);
}

TEST_F(GatewayTests, Second_Connect_Does_Not_Request_WebSocket_Endpoint_At_First) {
    // Arrange
    ASSERT_TRUE(Connect());
    gateway.Disconnect();

    // Act
    connected = gateway.Connect(connections, "DiscordBot");

    // Assert
    EXPECT_FALSE(connections->RequireResourceRequests(2));
}

TEST_F(GatewayTests, Second_Connect_Requests_WebSocket) {
    // Arrange
    ASSERT_TRUE(Connect());
    gateway.Disconnect();

    // Act
    connected = gateway.Connect(connections, "DiscordBot");

    // Assert
    EXPECT_TRUE(connections->RequireWebSocketRequests(2));
}

TEST_F(GatewayTests, Second_Connect_Requests_WebSocket_Endpoint_If_WebSocket_Open_Fails) {
    // Arrange
    ASSERT_TRUE(Connect());
    gateway.Disconnect();
    connected = gateway.Connect(connections, "DiscordBot");

    // Act
    ASSERT_TRUE(connections->RequireWebSocketRequests(2));
    connections->RespondToWebSocketRequest(1, nullptr);
    const auto resourceRequested = connections->RequireResourceRequests(2);

    // Assert
    EXPECT_TRUE(resourceRequested);
}

TEST_F(GatewayTests, Second_Connect_Second_WebSocket_Attempt_When_First_WebSocket_Open_Fails) {
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

TEST_F(GatewayTests, Second_Connect_Succeeds_After_Second_WebSocket_Connected_When_First_WebSocket_Open_Fails) {
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

TEST_F(GatewayTests, Second_Connect_Fails_After_Failed_Second_WebSocket_Attempt_When_First_WebSocket_Open_Fails) {
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

TEST_F(GatewayTests, Close_Callback_When_WebSocket_Closed_After_Callback_Registered) {
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

TEST_F(GatewayTests, Close_Callback_When_WebSocket_Closed_Before_Callback_Registered) {
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
