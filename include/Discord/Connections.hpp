#pragma once

/**
 * @file Connections.hpp
 *
 * This module declares the Discord::Connections interface.
 *
 * © 2020 by Richard Walters
 */

#include "WebSocket.hpp"

#include <functional>
#include <future>
#include <string>
#include <utility>
#include <vector>

namespace Discord {

    /**
     * This interface represents the networking dependencies of
     * the library, used to communicate with Discord online.
     */
    class Connections {
        // Types
    public:
        using CancelDelegate = std::function< void() >;

        struct Header {
            std::string key;
            std::string value;
        };

        struct ResourceRequest {
            std::string method;
            std::string uri;
            std::vector< Header > headers;
            std::string body;
        };

        struct Response {
            unsigned int status;
            std::vector< Header > headers;
            std::string body;
        };

        struct ResourceRequestTransaction {
            std::future< Response > response;
            CancelDelegate cancel;
        };

        struct WebSocketRequest {
            std::string uri;
        };

        struct WebSocketRequestTransaction {
            std::future< std::shared_ptr< WebSocket > > webSocket;
            CancelDelegate cancel;
        };

        // Methods
    public:
        virtual ResourceRequestTransaction QueueResourceRequest(
            const ResourceRequest& request
        ) = 0;

        virtual WebSocketRequestTransaction QueueWebSocketRequest(
            const WebSocketRequest& request
        ) = 0;
    };

}
