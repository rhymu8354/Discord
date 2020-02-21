#pragma once

/**
 * @file Common.hpp
 *
 * This module declares the common base class for test fixtures
 * and mocks used to test the Discord library.
 *
 * © 2020 by Richard Walters
 */

#include <gtest/gtest.h>
#include <Discord/Gateway.hpp>
#include <future>
#include <Json/Value.hpp>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <Timekeeping/Clock.hpp>
#include <vector>

/**
 * This is a fake WebSocket dependency which is used to test the Gateway
 * class.
 */
struct MockWebSocket
    : public Discord::WebSocket
{
    // Properties

    unsigned int closeCode = 0;
    bool closed = false;
    std::mutex mutex;
    CloseCallback onClose;
    ReceiveCallback onText;
    std::vector< std::string > textSent;
    size_t numTextsSentAwaiting = 0;
    std::shared_ptr< std::promise< void > > allTextsSent;

    // Methods

    bool AwaitTexts(size_t numTexts);
    void RemoteClose();

    // Discord::WebSocket

    virtual void Binary(std::string&& message) override;
    virtual void Close(unsigned int code) override;
    virtual void Text(std::string&& message) override;
    virtual void RegisterBinaryCallback(ReceiveCallback&& onBinary) override;
    virtual void RegisterCloseCallback(CloseCallback&& onClose) override;
    virtual void RegisterTextCallback(ReceiveCallback&& onText) override;
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
        std::promise< void > canceled;
        bool responded = false;
    };

    struct WebSocketRequestsWait {
        size_t numRequests = 0;
        std::promise< void > haveRequiredRequests;
    };

    struct WebSocketRequestWithPromise {
        WebSocketRequest request;
        std::promise< std::shared_ptr< Discord::WebSocket > > webSocketPromise;
        std::promise< void > canceled;
        bool responded = false;
    };

    // Properties

    std::mutex mutex;
    bool tornDown = false;
    std::vector< std::shared_ptr< ResourceRequestWithPromise > > resourceRequests;
    ResourceRequestsWait resourceRequestsWait;
    std::vector< std::shared_ptr< WebSocketRequestWithPromise > > webSocketRequests;
    WebSocketRequestsWait webSocketRequestsWait;

    // Methods

    bool ExpectSoon(
        std::promise< void >& asyncResult,
        std::unique_lock< decltype(mutex) >& lock
    );
    bool RequireResourceRequests(size_t numRequests);
    bool RequireWebSocketRequests(size_t numRequests);
    void RespondToResourceRequest(
        size_t requestIndex,
        Response&& response
    );
    void RespondToWebSocketRequest(
        size_t requestIndex,
        std::shared_ptr< Discord::WebSocket > webSocket
    );
    void TearDown();

    // Discord::Connections

    virtual ResourceRequestTransaction QueueResourceRequest(
        const ResourceRequest& request
    ) override;
    virtual WebSocketRequestTransaction QueueWebSocketRequest(
        const WebSocketRequest& request
    ) override;
};

/**
 * This is a fake clock which is used to test the Gateway class.
 */
struct MockClock
    : public Timekeeping::Clock
{
    // Properties

    double currentTime = 0.0;

    // Methods

    // Timekeeping::Clock

    virtual double GetCurrentTime() override;
};

/**
 * This is the base class for test fixtures used to test the Discord library.
 */
struct CommonTextFixture
    : public ::testing::Test
{
    // Properties

    /**
     * This is the unit under test.
     */
    std::future< bool > connected;

    std::shared_ptr< MockConnections > connections = std::make_shared< MockConnections >();
    int heartbeatIntervalMilliseconds = 45000;
    Discord::Gateway gateway;
    std::shared_ptr< MockClock > clock = std::make_shared< MockClock >();
    std::shared_ptr< Timekeeping::Scheduler > scheduler = std::make_shared< Timekeeping::Scheduler >();
    std::shared_ptr< MockWebSocket > webSocket = std::make_shared< MockWebSocket >();

    // Methods

    bool ConnectExpectingWebSocketEndpointRequestWithResponse(
        const std::string& webSocketEndpointRequestResponse,
        std::future< bool >& connected
    );
    bool Connect(const std::string webSocketEndpoint = "wss://gateway.discord.gg");
    void ExpectHeaders(
        const std::vector< Discord::Connections::Header >& expected,
        const std::vector< Discord::Connections::Header >& actual
    );
    void SendHello();
    void SendHeartbeatAck();

    // ::testing::Test

    virtual void SetUp() override;
    virtual void TearDown() override;
};
