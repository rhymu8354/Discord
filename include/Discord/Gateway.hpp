#pragma once

/**
 * @file Gateway.hpp
 *
 * This module declares the Discord::Gateway interface.
 *
 * © 2020 by Richard Walters
 */

#include "Connections.hpp"
#include "TimeKeeper.hpp"

#include <future>
#include <memory>

namespace Discord {

    /**
     * This is used to communicate with Discord's gateway services.
     */
    class Gateway {
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

        void SetTimeKeeper(const std::shared_ptr< TimeKeeper >& timeKeeper);

        std::future< bool > Connect(
            const std::shared_ptr< Connections >& connections,
            const std::string& userAgent
        );

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