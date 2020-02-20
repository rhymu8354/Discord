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
#include <StringExtensions/StringExtensions.hpp>
#include <unordered_map>
#include <vector>

namespace Discord {

    /**
     * This contains the private properties of a Gateway instance.
     */
    struct Gateway::Impl
        : public std::enable_shared_from_this< Impl >
    {
        // Types

        struct DiagnosticMessage {
            size_t level = 0;
            std::string message;
        };
        using MessageHandler = void (Impl::*)(
            Json::Value&& message,
            std::unique_lock< std::recursive_mutex >& lock
        );

        // Properties

        bool cancelConnection = false;
        Connections::CancelDelegate cancelCurrentOperation;
        bool closed = false;
        bool connecting = false;
        double heartbeatInterval = 0.0;
        std::recursive_mutex mutex;
        CloseCallback onClose;
        DiagnosticCallback onDiagnosticMessage;
        std::unique_ptr< std::future< void > > proceedWithConnect;
        int lastSequenceNumber = 0;
        bool receivedSequenceNumber = false;
        std::vector< DiagnosticMessage > storedDiagnosticMessages;
        std::shared_ptr< WebSocket > webSocket;
        std::string webSocketEndpoint;

        // Methods

        Connections::Response AwaitResourceRequest(
            const std::shared_ptr< Connections >& connections,
            Connections::ResourceRequest&& request,
            std::unique_lock< decltype(mutex) >& lock
        ) {
            if (cancelConnection) {
                return {499};
            }
            auto transaction = connections->QueueResourceRequest(request);
            cancelCurrentOperation = transaction.cancel;
            lock.unlock();
            auto response = transaction.response.get();
            lock.lock();
            cancelCurrentOperation = nullptr;
            if (cancelConnection) {
                response.status = 499;
            }
            return response;
        }

        std::shared_ptr< WebSocket > AwaitWebSocketRequest(
            const std::shared_ptr< Connections >& connections,
            Connections::WebSocketRequest&& request,
            std::unique_lock< decltype(mutex) >& lock
        ) {
            if (cancelConnection) {
                return nullptr;
            }
            auto transaction = connections->QueueWebSocketRequest(request);
            cancelCurrentOperation = transaction.cancel;
            lock.unlock();
            const auto webSocket = transaction.webSocket.get();
            lock.lock();
            cancelCurrentOperation = nullptr;
            if (cancelConnection) {
                return nullptr;
            }
            return webSocket;
        }

        std::string GetGateway(
            const std::shared_ptr< Connections >& connections,
            const std::string& userAgent,
            std::unique_lock< decltype(mutex) >& lock
        ) {
            const auto response = AwaitResourceRequest(
                connections,
                {
                    "GET",
                    "https://discordapp.com/api/v6/gateway",
                    {
                        {"User-Agent", userAgent},
                    },
                },
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
            // If told to wait before connecting, wait now.
            if (proceedWithConnect != nullptr) {
                decltype(proceedWithConnect) lastProceedWithConnect;
                lastProceedWithConnect.swap(proceedWithConnect);
                lock.unlock();
                lastProceedWithConnect.get();
                lock.lock();
            }

            // If we have a cache of the WebSocket URL, try to
            // use it now to open a WebSocket.
            static const std::string webSocketEndpointSuffix = "/?v=6&encoding=json";
            if (!webSocketEndpoint.empty()) {
                webSocket = AwaitWebSocketRequest(
                    connections,
                    {webSocketEndpoint + webSocketEndpointSuffix},
                    lock
                );
            }

            // If we don't have a WebSocket (either we didn't know the
            // URL, or the attempt to open one using a cached URL failed)
            if (!webSocket) {
                // Use the GetGateway API to find out
                // what the WebSocket URL is.
                webSocketEndpoint = GetGateway(connections, userAgent, lock);
                if (webSocketEndpoint.empty()) {
                    return false;
                }

                // Now try to open a WebSocket.
                webSocket = AwaitWebSocketRequest(
                    connections,
                    {webSocketEndpoint + webSocketEndpointSuffix},
                    lock
                );
            }

            // If we couldn't open a WebSocket by this point, we fail.
            if (!webSocket) {
                return false;
            }

            // Set up to receive close events as well as text and binary
            // messages from the gateway.
            NotifyDiagnosticMessage(
                0,
                "Connected to Discord",
                lock
            );
            RegisterWebSocketCallbacks();
            return true;
        }

        std::future< bool > Connect(
            const std::shared_ptr< Connections >& connections,
            const std::string& userAgent
        ) {
            if (webSocket || connecting) {
                std::promise< bool > alreadyConnecting;
                alreadyConnecting.set_value(false);
                return alreadyConnecting.get_future();
            }
            closed = false;
            connecting = true;
            cancelConnection = false;
            auto impl(shared_from_this());
            return std::async(
                std::launch::async,
                [impl, connections, userAgent]{
                    return impl->ConnectAsync(connections, userAgent);
                }
            );
        }

        bool ConnectAsync(
            const std::shared_ptr< Connections >& connections,
            const std::string& userAgent
        ) {
            std::unique_lock< decltype(mutex) > lock(mutex);
            const auto connected = CompleteConnect(
                connections,
                userAgent,
                lock
            );
            connecting = false;
            return connected;
        }

        void Disconnect() {
            cancelConnection = true;
            if (cancelCurrentOperation != nullptr) {
                cancelCurrentOperation();
            }
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

        void NotifyDiagnosticMessage(
            size_t level,
            std::string&& message,
            std::unique_lock< decltype(mutex) >& lock
        ) {
            decltype(onDiagnosticMessage) onDiagnosticMessageSample(onDiagnosticMessage);
            lock.unlock();
            if (onDiagnosticMessageSample == nullptr) {
                lock.lock();
                DiagnosticMessage messageInfo;
                messageInfo.level = level;
                messageInfo.message = std::move(message);
                storedDiagnosticMessages.push_back(std::move(messageInfo));
            } else {
                onDiagnosticMessageSample(
                    level,
                    std::move(message)
                );
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

        void OnHeartbeat(
            Json::Value&& message,
            std::unique_lock< decltype(mutex) >& lock
        ) {
            NotifyDiagnosticMessage(
                0,
                "Received heartbeat",
                lock
            );
            webSocket->Text(
                Json::Object({
                    {"op", 1},
                    {"d", (
                        receivedSequenceNumber
                        ? Json::Value(lastSequenceNumber)
                        : Json::Value(nullptr)
                    )},
                }).ToEncoding()
            );
        }

        void OnHello(
            Json::Value&& message,
            std::unique_lock< decltype(mutex) >& lock
        ) {
            // Discord tells us the interval in milliseconds.
            // We store it as a floating-point number of seconds.
            heartbeatInterval = (
                (double)message["d"]["heartbeat_interval"]
                / 1000.0
            );
            NotifyDiagnosticMessage(
                0,
                StringExtensions::sprintf(
                    "Heartbeat interval is %lg seconds",
                    heartbeatInterval
                ),
                lock
            );

            // Begin sending regular heartbeats.
            SendHeartbeat();
        }

        void OnText(
            std::string&& message,
            std::unique_lock< decltype(mutex) >& lock
        ) {
            // Interpret message JSON
            auto messageJson = Json::Value::FromEncoding(message);
            if (messageJson.GetType() != Json::Value::Type::Object) {
                NotifyDiagnosticMessage(
                    10,
                    StringExtensions::sprintf(
                        "Invalid text received: \"%s\"",
                        message.c_str()
                    ),
                    lock
                );
                return;
            }

            // Report the raw message via the diagnostic message hook.
            NotifyDiagnosticMessage(
                0,
                StringExtensions::sprintf(
                    "Received text: \"%s\"",
                    message.c_str()
                ),
                lock
            );

            // Dispatch based on opcode.
            static const std::unordered_map< int, MessageHandler > messageHandlersByOpcode = {
                {1, &Impl::OnHeartbeat},
                {10, &Impl::OnHello},
            };
            const int opcode = messageJson["op"];
            const auto messageHandlersByOpcodeEntry = messageHandlersByOpcode.find(opcode);
            if (messageHandlersByOpcodeEntry == messageHandlersByOpcode.end()) {
                NotifyDiagnosticMessage(
                    5,
                    StringExtensions::sprintf(
                        "Received message with unknown opcode %d",
                        opcode
                    ),
                    lock
                );
            } else {
                const auto messageHandler = messageHandlersByOpcodeEntry->second;
                (this->*messageHandler)(std::move(messageJson), lock);
            }
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
            webSocket->RegisterTextCallback(
                [weakSelf](std::string&& message){
                    const auto self = weakSelf.lock();
                    if (self == nullptr) {
                        return;
                    }
                    std::unique_lock< decltype(self->mutex) > lock(self->mutex);
                    self->OnText(std::move(message), lock);
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

        void RegisterDiagnosticMessageCallback(
            DiagnosticCallback&& onDiagnosticMessage,
            std::unique_lock< decltype(mutex) >& lock
        ) {
            this->onDiagnosticMessage = onDiagnosticMessage;
            if (
                !storedDiagnosticMessages.empty()
                && (this->onDiagnosticMessage != nullptr)
            ) {
                decltype(this->storedDiagnosticMessages) storedDiagnosticMessages;
                storedDiagnosticMessages.swap(this->storedDiagnosticMessages);
                decltype(this->onDiagnosticMessage) onDiagnosticMessageSample(this->onDiagnosticMessage);
                lock.unlock();
                for (auto& messageInfo: storedDiagnosticMessages) {
                    onDiagnosticMessageSample(
                        messageInfo.level,
                        std::move(messageInfo.message)
                    );
                }
                lock.lock();
            }
        }

        void SendHeartbeat() {
            webSocket->Text(
                Json::Object({
                    {"op", 1},
                    {"d", (
                        receivedSequenceNumber
                        ? Json::Value(lastSequenceNumber)
                        : Json::Value(nullptr)
                    )},
                }).ToEncoding()
            );
        }

        void WaitBeforeConnect(std::future< void >&& proceedWithConnect) {
            this->proceedWithConnect.reset(
                new std::future< void >(std::move(proceedWithConnect))
            );
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

    void Gateway::WaitBeforeConnect(std::future< void >&& proceedWithConnect) {
        std::lock_guard< decltype(impl_->mutex) > lock(impl_->mutex);
        impl_->WaitBeforeConnect(std::move(proceedWithConnect));
    }

    std::future< bool > Gateway::Connect(
        const std::shared_ptr< Connections >& connections,
        const std::string& userAgent
    ) {
        std::lock_guard< decltype(impl_->mutex) > lock(impl_->mutex);
        return impl_->Connect(connections, userAgent);
    }

    void Gateway::RegisterCloseCallback(CloseCallback&& onClose) {
        std::unique_lock< decltype(impl_->mutex) > lock(impl_->mutex);
        impl_->RegisterCloseCallback(std::move(onClose), lock);
    }

    void Gateway::RegisterDiagnosticMessageCallback(DiagnosticCallback&& onDiagnosticMessage) {
        std::unique_lock< decltype(impl_->mutex) > lock(impl_->mutex);
        impl_->RegisterDiagnosticMessageCallback(std::move(onDiagnosticMessage), lock);
    }

    void Gateway::Disconnect() {
        std::lock_guard< decltype(impl_->mutex) > lock(impl_->mutex);
        impl_->Disconnect();
    }

}
