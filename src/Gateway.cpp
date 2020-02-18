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
    struct Gateway::Impl
        : public std::enable_shared_from_this< Impl >
    {
        // Properties

        bool closed = false;
        bool connecting = false;
        std::mutex mutex;
        CloseCallback onClose;
        std::shared_ptr< WebSocket > webSocket;
        std::string webSocketEndpoint;

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
            if (!webSocketEndpoint.empty()) {
                webSocket = Await(
                    connections->QueueWebSocketRequest({webSocketEndpoint}),
                    lock
                );
            }
            if (!webSocket) {
                webSocketEndpoint = GetGateway(connections, userAgent, lock);
                if (webSocketEndpoint.empty()) {
                    return false;
                }
                webSocket = Await(
                    connections->QueueWebSocketRequest({
                        webSocketEndpoint + "/?v=6&encoding=json"
                    }),
                    lock
                );
            }
            if (webSocket) {
                RegisterWebSocketCallbacks();
                return true;
            } else {
                return false;
            }
        }

        bool Connect(
            const std::shared_ptr< Connections >& connections,
            const std::string& userAgent
        ) {
            std::unique_lock< decltype(mutex) > lock(mutex);
            if (webSocket || connecting) {
                return false;
            }
            closed = false;
            connecting = true;
            const auto connected = CompleteConnect(
                connections,
                userAgent,
                lock
            );
            connecting = false;
            return connected;
        }

        void Disconnect() {
            if (webSocket == nullptr) {
                return;
            }
            webSocket->Close();
            webSocket = nullptr;
        }

        void NotifyClose(std::unique_lock< decltype(mutex) >& lock) {
            CloseCallback onClose = this->onClose;
            if (onClose != nullptr) {
                lock.unlock();
                onClose();
                lock.lock();
            }
        }

        void OnClose(std::unique_lock< decltype(mutex) >& lock) {
            if (closed) {
                return;
            }
            closed = true;
            NotifyClose(lock);
        }

        void RegisterWebSocketCallbacks() {
            std::weak_ptr< Impl > weakSelf(shared_from_this());
            webSocket->RegisterCloseCallback(
                [weakSelf]{
                    const auto self = weakSelf.lock();
                    if (self == nullptr) {
                        return;
                    }
                    std::unique_lock< decltype(self->mutex) > lock(self->mutex);
                    self->OnClose(lock);
                }
            );
        }

        void RegisterCloseCallback(
            CloseCallback&& onClose,
            std::unique_lock< decltype(mutex) >& lock
        ) {
            this->onClose = std::move(onClose);
            if (closed) {
                NotifyClose(lock);
            }
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

    void Gateway::RegisterCloseCallback(CloseCallback&& onClose) {
        std::unique_lock< decltype(impl_->mutex) > lock(impl_->mutex);
        impl_->RegisterCloseCallback(std::move(onClose), lock);
    }

    void Gateway::Disconnect() {
        std::lock_guard< decltype(impl_->mutex) > lock(impl_->mutex);
        impl_->Disconnect();
    }

}
