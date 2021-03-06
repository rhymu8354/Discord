#pragma once

/**
 * @file Gateway.hpp
 *
 * This module declares the Discord::Gateway interface.
 *
 * © 2020 by Richard Walters
 */

#include "Connections.hpp"

#include <functional>
#include <future>
#include <memory>
#include <Timekeeping/Scheduler.hpp>

namespace Discord {

    /**
     * This is used to communicate with Discord's gateway services.
     */
    class Gateway {
        // Types
    public:
        using CloseCallback = std::function< void() >;
        struct Configuration {
            std::string browser;
            std::string device;
            std::string os;
            std::string token;
            std::string userAgent;
        };
        using DiagnosticCallback = std::function<
            void(
                size_t level,
                std::string&& message
            )
        >;

        // Lifecycle management
    public:
        ~Gateway() noexcept;
        Gateway(const Gateway& other) = delete;
        Gateway(Gateway&&) noexcept;
        Gateway& operator=(const Gateway& other) = delete;
        Gateway& operator=(Gateway&&) noexcept;

        // Public methods
    public:
        /**
         * This is the default constructor.
         */
        Gateway();

        void SetScheduler(const std::shared_ptr< Timekeeping::Scheduler >& scheduler);

        void WaitBeforeConnect(std::future< void >&& proceedWithConnect);

        std::future< bool > Connect(
            const std::shared_ptr< Connections >& connections,
            const Configuration& configuration
        );

        void RegisterCloseCallback(CloseCallback&& onClose);

        void RegisterDiagnosticMessageCallback(DiagnosticCallback&& onDiagnosticMessage);

        void Disconnect();

        // Private properties
    private:
        /**
         * This is the type of structure that contains the private
         * properties of the instance.  It is defined in the implementation
         * and declared here to ensure that it is scoped inside the class.
         */
        struct Impl;

        /**
         * This contains the private properties of the instance.
         */
        std::shared_ptr< Impl > impl_;
    };

}
