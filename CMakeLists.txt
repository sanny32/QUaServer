cmake_minimum_required(VERSION 3.21)

project(QUaServer
    VERSION 2.0.0
    DESCRIPTION "Qt C++ wrapper for the open62541 OPC UA server library"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# The static libraries may end up in a host project's shared library or plugin
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(PROJECT_IS_TOP_LEVEL)
    if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endif()
    if(NOT CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    endif()
    if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    endif()
endif()

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------
option(QUASERVER_NAMESPACE_FULL      "Build open62541 with the full namespace zero (ua_namespace_full)"   OFF)
option(QUASERVER_ENCRYPTION          "Enable encryption support using OpenSSL (ua_encryption)"            OFF)
option(QUASERVER_EVENTS              "Enable events support (ua_events)"                                  OFF)
option(QUASERVER_ALARMS_CONDITIONS   "Enable alarms & conditions support (ua_alarms_conditions)"          OFF)
option(QUASERVER_HISTORIZING         "Enable historizing support (ua_historizing)"                        OFF)
option(QUASERVER_BUILD_EXAMPLES      "Build the examples"                                                 ${PROJECT_IS_TOP_LEVEL})
option(QUASERVER_BUILD_TESTS         "Build the tests"                                                    OFF)

# Alarms & conditions require events, events require the full namespace zero
if(QUASERVER_ALARMS_CONDITIONS)
    set(QUASERVER_EVENTS ON)
endif()
if(QUASERVER_EVENTS)
    set(QUASERVER_NAMESPACE_FULL ON)
endif()

find_package(Qt6 6.9 REQUIRED COMPONENTS Core)

include(cmake/Dependencies.cmake)

add_subdirectory(src/wrapper)

if(QUASERVER_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(QUASERVER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(src/tests)
endif()

message(STATUS "QUaServer configuration:")
message(STATUS "  Qt version             : ${Qt6_VERSION}")
message(STATUS "  Namespace zero full    : ${QUASERVER_NAMESPACE_FULL}")
message(STATUS "  Encryption (OpenSSL)   : ${QUASERVER_ENCRYPTION}")
message(STATUS "  Events                 : ${QUASERVER_EVENTS}")
message(STATUS "  Alarms & conditions    : ${QUASERVER_ALARMS_CONDITIONS}")
message(STATUS "  Historizing            : ${QUASERVER_HISTORIZING}")
message(STATUS "  Examples               : ${QUASERVER_BUILD_EXAMPLES}")
message(STATUS "  Tests                  : ${QUASERVER_BUILD_TESTS}")
