#pragma once

/**
 * @file TimeKeeper.hpp
 *
 * This module declares the Discord::TimeKeeper interface.
 *
 * © 2020 by Richard Walters
 */

namespace Discord {

    /**
     * This represents the time-keeping requirements of Discord classes.
     * To integrate Discord into a larger program, implement this
     * interface in terms of real time.
     */
    class TimeKeeper {
    public:
        // Methods

        /**
         * This method returns the current server time, in seconds.
         *
         * @return
         *     The current server time is returned, in seconds.
         */
        virtual double GetCurrentTime() = 0;
    };

}
