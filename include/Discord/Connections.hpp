#pragma once

/**
 * @file Connections.hpp
 *
 * This module declares the Discord::Connections interface.
 *
 * © 2020 by Richard Walters
 */

#include "WebSocket.hpp"

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

        struct WebSocketRequest {
            std::string uri;
        };

        struct Response {
            unsigned int status;
            std::vector< Header > headers;
            std::string body;
        };

        // Methods
    public:
        virtual std::future< Response > QueueResourceRequest(
            const ResourceRequest& request
        ) = 0;

        virtual std::future< std::shared_ptr< WebSocket > > QueueWebSocketRequest(
            const WebSocketRequest& request
        ) = 0;
    };

}
