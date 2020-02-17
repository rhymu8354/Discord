/**
 * @file GatewayTests.cpp
 *
 * This module contains the unit tests of the Discord::Gateway class.
 *
 * © 2020 by Richard Walters
 */

#include <gtest/gtest.h>
#include <Discord/Gateway.hpp>

/**
 * This is the test fixture for these tests, providing common
 * setup and teardown for each test.
 */
struct GatewayTests
    : public ::testing::Test
{
    // Properties

    /**
     * This is the unit under test.
     */
    Discord::Gateway gateway;

    // ::testing::Test

    virtual void SetUp() override {
    }

    virtual void TearDown() override {
    }
};

TEST_F(GatewayTests, First) {
}
