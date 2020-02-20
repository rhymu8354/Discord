/**
 * @file Common.cpp
 *
 * This module contains definitions of methods of the common base class for
 * test fixtures and mocks used to test the Discord library.
 *
 * © 2020 by Richard Walters
 */

#include "Common.hpp"

#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <unordered_map>

bool MockWebSocket::AwaitTexts(size_t numTexts) {
    std::unique_lock< decltype(mutex) > lock(mutex);
    if (textSent.size() >= numTexts) {
        return true;
    }
    numTextsSentAwaiting = numTexts;
    allTextsSent = std::make_shared< std::promise< void > >();
    lock.unlock();
    return (
        allTextsSent->get_future().wait_for(
            std::chrono::milliseconds(200)
        )
        == std::future_status::ready
    );
}

void MockWebSocket::RemoteClose() {
    if (onClose != nullptr) {
        onClose();
    }
}

void MockWebSocket::Binary(std::string&& message) {
}

void MockWebSocket::Close() {
    closed = true;
    if (onClose != nullptr) {
        onClose();
    }
}

void MockWebSocket::Text(std::string&& message) {
    std::lock_guard< decltype(mutex) > lock(mutex);
    textSent.push_back(std::move(message));
    if (
        (allTextsSent != nullptr)
        && (textSent.size() == numTextsSentAwaiting)
    ) {
        allTextsSent->set_value();
        allTextsSent = nullptr;
    }
}

void MockWebSocket::RegisterBinaryCallback(ReceiveCallback&& onBinary) {
}

void MockWebSocket::RegisterCloseCallback(CloseCallback&& onClose) {
    this->onClose = std::move(onClose);
}

void MockWebSocket::RegisterTextCallback(ReceiveCallback&& onText) {
    this->onText = std::move(onText);
}

bool MockConnections::ExpectSoon(
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

bool MockConnections::RequireResourceRequests(size_t numRequests) {
    std::unique_lock< decltype(mutex) > lock(mutex);
    if (resourceRequests.size() >= numRequests) {
        return true;
    }
    resourceRequestsWait.numRequests = numRequests;
    resourceRequestsWait.haveRequiredRequests = decltype(resourceRequestsWait.haveRequiredRequests)();
    return ExpectSoon(resourceRequestsWait.haveRequiredRequests, lock);
}

bool MockConnections::RequireWebSocketRequests(size_t numRequests) {
    std::unique_lock< decltype(mutex) > lock(mutex);
    if (webSocketRequests.size() >= numRequests) {
        return true;
    }
    webSocketRequestsWait.numRequests = numRequests;
    webSocketRequestsWait.haveRequiredRequests = decltype(webSocketRequestsWait.haveRequiredRequests)();
    return ExpectSoon(webSocketRequestsWait.haveRequiredRequests, lock);
}

void MockConnections::RespondToResourceRequest(
    size_t requestIndex,
    Response&& response
) {
    resourceRequests[requestIndex]->responsePromise.set_value(std::move(response));
    resourceRequests[requestIndex]->responded = true;
}

void MockConnections::RespondToWebSocketRequest(
    size_t requestIndex,
    std::shared_ptr< Discord::WebSocket > webSocket
) {
    webSocketRequests[requestIndex]->webSocketPromise.set_value(std::move(webSocket));
    webSocketRequests[requestIndex]->responded = true;
}

void MockConnections::TearDown() {
    std::lock_guard< decltype(mutex) > lock(mutex);
    tornDown = true;
    for (auto& request: resourceRequests) {
        if (!request->responded) {
            request->responsePromise.set_value({500});
        }
    }
    for (auto& request: webSocketRequests) {
        if (!request->responded) {
            request->webSocketPromise.set_value(nullptr);
        }
    }
}

auto MockConnections::QueueResourceRequest(
    const ResourceRequest& request
) -> ResourceRequestTransaction{
    std::lock_guard< decltype(mutex) > lock(mutex);
    auto requestWithPromise = std::make_shared< ResourceRequestWithPromise >();
    requestWithPromise->request = request;
    ResourceRequestTransaction transaction;
    transaction.response = requestWithPromise->responsePromise.get_future();
    if (tornDown) {
        requestWithPromise->responsePromise.set_value({500});
        transaction.cancel = []{};
    } else {
        transaction.cancel = [requestWithPromise]{
            requestWithPromise->responsePromise.set_value({499});
            requestWithPromise->responded = true;
            requestWithPromise->canceled.set_value();
        };
        resourceRequests.push_back(std::move(requestWithPromise));
        if (
            (resourceRequestsWait.numRequests > 0)
            && (resourceRequests.size() == resourceRequestsWait.numRequests)
        ) {
            resourceRequestsWait.haveRequiredRequests.set_value();
        }
    }
    return transaction;
}

auto MockConnections::QueueWebSocketRequest(
    const WebSocketRequest& request
) -> WebSocketRequestTransaction{
    std::lock_guard< decltype(mutex) > lock(mutex);
    auto requestWithPromise = std::make_shared< WebSocketRequestWithPromise >();
    requestWithPromise->request = request;
    WebSocketRequestTransaction transaction;
    transaction.webSocket = requestWithPromise->webSocketPromise.get_future();
    if (tornDown) {
        requestWithPromise->webSocketPromise.set_value(nullptr);
        transaction.cancel = []{};
    } else {
        transaction.cancel = [requestWithPromise]{
            requestWithPromise->webSocketPromise.set_value(nullptr);
            requestWithPromise->responded = true;
            requestWithPromise->canceled.set_value();
        };
        webSocketRequests.push_back(std::move(requestWithPromise));
        if (
            (webSocketRequestsWait.numRequests > 0)
            && (webSocketRequests.size() == webSocketRequestsWait.numRequests)
        ) {
            webSocketRequestsWait.haveRequiredRequests.set_value();
        }
    }
    return transaction;
}

double MockClock::GetCurrentTime() {
    return currentTime;
}

bool CommonTextFixture::ConnectExpectingWebSocketEndpointRequestWithResponse(
    const std::string& webSocketEndpointRequestResponse,
    std::future< bool >& connected
) {
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

bool CommonTextFixture::Connect(const std::string webSocketEndpoint) {
    const auto nextWebSocketRequest = connections->webSocketRequests.size();
    const auto resourceRequested = ConnectExpectingWebSocketEndpointRequestWithResponse(
        Json::Object({
            {"url", webSocketEndpoint},
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

void CommonTextFixture::ExpectHeaders(
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

void CommonTextFixture::SendHello() {
    webSocket->onText(
        Json::Object({
            {"op", 10},
            {"d", Json::Object({
                {"heartbeat_interval", heartbeatIntervalMilliseconds},
            })},
        }).ToEncoding()
    );
}

void CommonTextFixture::SetUp() {
    ::testing::Test::SetUp();
    scheduler->SetClock(clock);
    gateway.SetScheduler(scheduler);
}

void CommonTextFixture::TearDown() {
    connections->TearDown();
    ::testing::Test::TearDown();
}
