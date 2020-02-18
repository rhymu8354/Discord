/**
 * @file Gateway.cpp
 *
 * This module contains the implementation of the
 * Discord::Gateway class.
 *
 * © 2020 by Richard Walters
 */

#include <Discord/Gateway.hpp>
#include <future>
#include <Json/Value.hpp>
#include <memory>
#include <mutex>

namespace Discord {

    /**
     * This contains the private properties of a Gateway instance.
     */
    struct Gateway::Impl {
        // Properties

        bool connecting = false;
        std::mutex mutex;
        std::shared_ptr< WebSocket > webSocket;

        // Methods

        template< typename T > T Await(
            std::future< T >& asyncResult,
            std::unique_lock< decltype(mutex) >& lock
        ) {
            lock.unlock();
            const auto result = asyncResult.get();
            lock.lock();
            return result;
        }

        std::string GetGateway(
            const std::shared_ptr< Connections >& connections,
            const std::string& userAgent,
            std::unique_lock< decltype(mutex) >& lock
        ) {
            const auto response = Await(
                connections->QueueResourceRequest({
                    "GET",
                    "https://discordapp.com/api/v6/gateway",
                    {
                        {"User-Agent", userAgent},
                    },
                }),
                lock
            );
            if (response.status != 200) {
                return "";
            }
            return Json::Value::FromEncoding(response.body)["url"];
        }

        bool CompleteConnect(
            const std::shared_ptr< Connections >& connections,
            const std::string& userAgent,
            std::unique_lock< decltype(mutex) >& lock
        ) {
            const auto url = GetGateway(connections, userAgent, lock);
            if (url.empty()) {
                return false;
            }
            const auto response = Await(
                connections->QueueWebSocketRequest({url}),
                lock
            );
            if (response == nullptr) {
                return false;
            }
            return true;
        }

        bool Connect(
            const std::shared_ptr< Connections >& connections,
            const std::string& userAgent
        ) {
            std::unique_lock< decltype(mutex) > lock(mutex);
            if (webSocket || connecting) {
                return false;
            }
            connecting = true;
            const auto connected = CompleteConnect(
                connections,
                userAgent,
                lock
            );
            connecting = false;
            return connected;
        }
    };

    Gateway::~Gateway() noexcept = default;
    Gateway::Gateway(Gateway&&) noexcept = default;
    Gateway& Gateway::operator=(Gateway&&) noexcept = default;

    Gateway::Gateway()
        : impl_(new Impl())
    {
    }

    void Gateway::SetTimeKeeper(const std::shared_ptr< TimeKeeper >& timeKeeper) {
    }

    std::future< bool > Gateway::Connect(
        const std::shared_ptr< Connections >& connections,
        const std::string& userAgent
    ) {
        std::lock_guard< decltype(impl_->mutex) > lock(impl_->mutex);
        auto impl(impl_);
        return std::async(
            std::launch::async,
            [impl, connections, userAgent]{
                return impl->Connect(connections, userAgent);
            }
        );
    }

    void Gateway::Disconnect() {
    }

}
