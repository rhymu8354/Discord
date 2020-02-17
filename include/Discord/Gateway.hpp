#pragma once

/**
 * @file Gateway.hpp
 *
 * This module declares the Discord::Gateway interface.
 *
 * © 2020 by Richard Walters
 */

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
        std::unique_ptr< Impl > impl_;
    };

}
