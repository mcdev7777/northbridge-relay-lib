cmake_minimum_required(VERSION 3.18)
project(northbridge_relay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(northbridge_relay STATIC
    src/relay.cpp
    src/socket_util.cpp
    src/wire.cpp
)

target_include_directories(northbridge_relay
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if (WIN32)
    target_link_libraries(northbridge_relay PRIVATE ws2_32)
else()
    message(FATAL_ERROR "northbridge_relay is intended for Windows builds")
endif()

add_executable(relay_edge_host tools/relay_edge_host.cpp)
target_link_libraries(relay_edge_host PRIVATE northbridge_relay)

add_executable(relay_core_host tools/relay_core_host.cpp)
target_link_libraries(relay_core_host PRIVATE northbridge_relay)
