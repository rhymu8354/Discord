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
#include <Timekeeping/Scheduler.hpp>
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

        bool awaitingHello = false;
        bool disconnect = false;
        Connections::CancelDelegate cancelCurrentOperation;
        bool closed = false;
        std::promise< void > closePromise;
        bool connecting = false;
        bool heartbeatAckReceived = false;
        double heartbeatInterval = 0.0;
        int heartbeatSchedulerToken = 0;
        std::promise< void > helloPromise;
        std::recursive_mutex mutex;
        CloseCallback onClose;
        DiagnosticCallback onDiagnosticMessage;
        std::unique_ptr< std::future< void > > proceedWithConnect;
        int lastSequenceNumber = 0;
        double nextHeartbeatTime = 0.0;
        bool receivedSequenceNumber = false;
        std::shared_ptr< Timekeeping::Scheduler > scheduler;
        std::vector< DiagnosticMessage > storedDiagnosticMessages;
        std::shared_ptr< WebSocket > webSocket;
        std::string webSocketEndpoint;

        // Methods

        void AwaitHelloPromise(std::unique_lock< decltype(mutex) >& lock) {
            cancelCurrentOperation = [&]{
                helloPromise.set_value();
            };
            lock.unlock();
            helloPromise.get_future().wait();
            lock.lock();
            cancelCurrentOperation = nullptr;
        }

        Connections::Response AwaitResourceRequest(
            const std::shared_ptr< Connections >& connections,
            Connections::ResourceRequest&& request,
            std::unique_lock< decltype(mutex) >& lock
        ) {
            if (disconnect) {
                return {499};
            }
            auto transaction = connections->QueueResourceRequest(request);
            cancelCurrentOperation = transaction.cancel;
            lock.unlock();
            auto response = transaction.response.get();
            lock.lock();
            cancelCurrentOperation = nullptr;
            if (disconnect) {
                response.status = 499;
            }
            return response;
        }

        std::shared_ptr< WebSocket > AwaitWebSocketRequest(
            const std::shared_ptr< Connections >& connections,
            Connections::WebSocketRequest&& request,
            std::unique_lock< decltype(mutex) >& lock
        ) {
            if (disconnect) {
                return nullptr;
            }
            auto transaction = connections->QueueWebSocketRequest(request);
            cancelCurrentOperation = transaction.cancel;
            lock.unlock();
            const auto webSocket = transaction.webSocket.get();
            lock.lock();
            cancelCurrentOperation = nullptr;
            if (disconnect) {
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
            const Configuration& configuration,
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
                webSocketEndpoint = GetGateway(
                    connections,
                    configuration.userAgent,
                    lock
                );
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
            // messages from the gateway, expecting a "hello" message from the
            // gateway immediately afterward.
            awaitingHello = true;
            helloPromise = std::promise< void >();
            RegisterWebSocketCallbacks();
            AwaitHelloPromise(lock);
            if (disconnect) {
                return false;
            }

            // Send identify or resume message.
            SendIdentify(configuration, lock);

            // Wait for either ready or invalid session message.

            // If invalid session message received:
            // * If last message we sent was identify, fail connection.
            // * If last message we sent was resume, abandon session and wait
            //   1-5 seconds before trying again.

            // At this point the session is (re)established.
            NotifyDiagnosticMessage(
                1,
                "Connected to Discord",
                lock
            );
            return true;
        }

        std::future< bool > Connect(
            const std::shared_ptr< Connections >& connections,
            const Configuration& configuration
        ) {
            // Fail if no scheduler is set, or if we have a WebSocket or are in
            // the process of connecting.
            if (
                (scheduler == nullptr)
                || webSocket
                || connecting
            ) {
                std::promise< bool > alreadyConnecting;
                alreadyConnecting.set_value(false);
                return alreadyConnecting.get_future();
            }
            closed = false;
            closePromise = std::promise< void >();
            connecting = true;
            disconnect = false;
            auto impl(shared_from_this());
            return std::async(
                std::launch::async,
                [impl, connections, configuration]{
                    return impl->ConnectAsync(
                        connections,
                        configuration
                    );
                }
            );
        }

        bool ConnectAsync(
            const std::shared_ptr< Connections >& connections,
            const Configuration& configuration
        ) {
            std::unique_lock< decltype(mutex) > lock(mutex);
            const auto connected = CompleteConnect(
                connections,
                configuration,
                lock
            );
            connecting = false;
            return connected;
        }

        void Disconnect(std::unique_lock< decltype(mutex) >& lock) {
            disconnect = true;
            if (cancelCurrentOperation != nullptr) {
                cancelCurrentOperation();
            }
            if (webSocket == nullptr) {
                return;
            }
            webSocket->Close(1000);
            lock.unlock();
            const auto wasClosed = (
                closePromise.get_future().wait_for(
                    std::chrono::milliseconds(1000)
                )
                == std::future_status::ready
            );
            lock.lock();
            if (!wasClosed) {
                NotifyDiagnosticMessage(
                    5,
                    "Timeout waiting for Discord to close its end of the WebSocket",
                    lock
                );
            }
            UnscheduleAll();
            webSocket = nullptr;
            heartbeatInterval = 0.0;
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
            NotifyDiagnosticMessage(
                1,
                "Disconnected from Discord",
                lock
            );
            NotifyClose(lock);
            closePromise.set_value();
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
            SendHeartbeat(lock);
        }

        void OnHeartbeatAck(
            Json::Value&& message,
            std::unique_lock< decltype(mutex) >& lock
        ) {
            NotifyDiagnosticMessage(
                0,
                "Received heartbeat ACK",
                lock
            );
            heartbeatAckReceived = true;
        }

        void OnHeartbeatDue(std::unique_lock< decltype(mutex) >& lock) {
            heartbeatSchedulerToken = 0;
            if (
                !heartbeatAckReceived
                && (webSocket != nullptr)
                && !closed
            ) {
                // Use code 4000 because the Discord docs are vague
                // (they say use a "non-1000 close code)
                // and discord.py uses code 4000 and we want to be cool
                // just like them.
                //
                // See:
                // * https://discordapp.com/developers/docs/topics/gateway#connecting-to-the-gateway
                // * https://github.com/Rapptz/discord.py/blob/b9e6ed28a408cd2798f0a4d96b888c9ecf5e950a/discord/gateway.py#L85
                webSocket->Close(4000);
                OnClose(lock);
                return;
            }
            SendHeartbeat(lock);
        }

        void OnHello(
            Json::Value&& message,
            std::unique_lock< decltype(mutex) >& lock
        ) {
            // Catch and discard unexpected "hello" messages.
            if (!awaitingHello) {
                return;
            }
            awaitingHello = false;

            // Discord tells us the interval in milliseconds.
            // We store it as a floating-point number of seconds.
            heartbeatInterval = (
                (double)message["d"]["heartbeat_interval"]
                / 1000.0
            );
            NotifyDiagnosticMessage(
                1,
                StringExtensions::sprintf(
                    "Heartbeat interval is %lg seconds",
                    heartbeatInterval
                ),
                lock
            );

            // Begin sending regular heartbeats.
            SendHeartbeat(lock);

            // Unblock CompleteConnect to proceed to the next step in the
            // connection process now that a hello message has been received.
            helloPromise.set_value();
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
                {11, &Impl::OnHeartbeatAck},
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

        void ScheduleAll() {
            ScheduleHeartbeat();
        }

        void ScheduleHeartbeat() {
            if (
                (scheduler == nullptr)
                || (webSocket == nullptr)
                || closed
                || (heartbeatSchedulerToken != 0)
            ) {
                return;
            }
            std::weak_ptr< Impl > weakSelf(shared_from_this());
            heartbeatSchedulerToken = scheduler->Schedule(
                [weakSelf]{
                    const auto self = weakSelf.lock();
                    if (self == nullptr) {
                        return;
                    }
                    std::unique_lock< decltype(self->mutex) > lock(self->mutex);
                    self->OnHeartbeatDue(lock);
                },
                nextHeartbeatTime
            );
        }

        void SendHeartbeat(std::unique_lock< decltype(mutex) >& lock) {
            // Cancel any currently-scheduled heartbeat.
            UnscheduleHeartbeat();

            // Don't send a heartbeat if we have no WebSocket
            // or if it's closed.
            if (
                (webSocket == nullptr)
                || closed
            ) {
                return;
            }

            // Make it clear that we have not yet received the acknowlegment
            // for this heartbeat.
            heartbeatAckReceived = false;

            // Send a heartbeat to the gateway.
            NotifyDiagnosticMessage(0, "Sending heartbeat", lock);
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

            // If a heartbeat interval is set, schedule the next heartbeat.
            if (heartbeatInterval != 0.0) {
                nextHeartbeatTime += heartbeatInterval;
                const auto now = scheduler->GetClock()->GetCurrentTime();
                if (nextHeartbeatTime <= now) {
                    nextHeartbeatTime = now + heartbeatInterval;
                }
                ScheduleHeartbeat();
            }
        }

        void SendIdentify(
            const Configuration& configuration,
            std::unique_lock< decltype(mutex) >& lock
        ) {
            NotifyDiagnosticMessage(0, "Sending identify", lock);
            webSocket->Text(
                Json::Object({
                    {"op", 2},
                    {"d", Json::Object({
                        {"token", configuration.token},
                        {"properties", Json::Object({
                            {"$os", configuration.os},
                            {"$browser", configuration.browser},
                            {"$device", configuration.device},
                        })},
                    })},
                }).ToEncoding()
            );
        }

        void UnscheduleAll() {
            UnscheduleHeartbeat();
        }

        void UnscheduleHeartbeat() {
            if (
                (scheduler == nullptr)
                || (heartbeatSchedulerToken == 0)
            ) {
                return;
            }
            scheduler->Cancel(heartbeatSchedulerToken);
            heartbeatSchedulerToken = 0;
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

    void Gateway::SetScheduler(const std::shared_ptr< Timekeeping::Scheduler >& scheduler) {
        std::lock_guard< decltype(impl_->mutex) > lock(impl_->mutex);
        impl_->UnscheduleAll();
        impl_->scheduler = scheduler;
        impl_->ScheduleAll();
    }

    void Gateway::WaitBeforeConnect(std::future< void >&& proceedWithConnect) {
        std::lock_guard< decltype(impl_->mutex) > lock(impl_->mutex);
        impl_->WaitBeforeConnect(std::move(proceedWithConnect));
    }

    std::future< bool > Gateway::Connect(
        const std::shared_ptr< Connections >& connections,
        const Configuration& configuration
    ) {
        std::lock_guard< decltype(impl_->mutex) > lock(impl_->mutex);
        return impl_->Connect(connections, configuration);
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
        std::unique_lock< decltype(impl_->mutex) > lock(impl_->mutex);
        impl_->Disconnect(lock);
    }

}
