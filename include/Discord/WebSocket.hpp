#pragma once

/**
 * @file WebSocket.hpp
 *
 * This module declares the Discord::WebSocket interface.
 *
 * © 2020 by Richard Walters
 */

#include <functional>
#include <future>
#include <string>

namespace Discord {

    /**
     * This interface represents a WebSocket connection between
     * the library and Discord, from the perspective of the library
     * as a client.
     */
    class WebSocket {
        // Types
    public:
        using ReceiveCallback = std::function<
            void(
                std::string&& message
            )
        >;

        // Methods
    public:
        virtual void OnClose(std::promise< void >&& onClose) = 0;
        virtual void OnText(ReceiveCallback&& onText) = 0;
        virtual void OnBinary(ReceiveCallback&& onBinary) = 0;
        virtual void Close() = 0;
        virtual void Text(std::string&& message) = 0;
        virtual void Binary(std::string&& message) = 0;
    };

}
