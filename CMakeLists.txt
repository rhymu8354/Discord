# CMakeLists.txt for Discord
#
# © 2020 by Richard Walters

cmake_minimum_required(VERSION 3.8)
set(This Discord)

set(Headers
    include/Discord/Connections.hpp
    include/Discord/Gateway.hpp
    include/Discord/TimeKeeper.hpp
    include/Discord/WebSocket.hpp
)

set(Sources
    src/Gateway.cpp
)

add_library(${This} STATIC ${Sources} ${Headers})
set_target_properties(${This} PROPERTIES
    FOLDER Libraries
)

target_include_directories(${This} PUBLIC include)

target_link_libraries(${This} PUBLIC
    Json
)

add_subdirectory(test)
