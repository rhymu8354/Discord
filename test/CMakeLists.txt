# CMakeLists.txt for DiscordTests
#
# © 2020 by Richard Walters

cmake_minimum_required(VERSION 3.8)
set(This DiscordTests)

set(Sources
    src/Common.cpp
    src/Common.hpp
    src/ConnectionTests.cpp
    src/HeartbeatTests.cpp
)

add_executable(${This} ${Sources})
set_target_properties(${This} PROPERTIES
    FOLDER Tests
)

target_include_directories(${This} PRIVATE ..)

target_link_libraries(${This} PUBLIC
    gtest_main
    Discord
    Json
)

add_test(
    NAME ${This}
    COMMAND ${This}
)
