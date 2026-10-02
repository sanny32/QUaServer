# Builds the third-party dependencies (mbedTLS, open62541) from the git
# submodules in depends/ as part of the main build.

set(QUASERVER_DEPENDS_DIR ${PROJECT_SOURCE_DIR}/depends)

if(NOT EXISTS ${QUASERVER_DEPENDS_DIR}/open62541.git/CMakeLists.txt)
    message(FATAL_ERROR
        "open62541 submodule is missing. Run:\n"
        "  git submodule update --init --recursive")
endif()

# Helper to force a cache option of a subproject
macro(quaserver_set_cache name value type)
    set(${name} ${value} CACHE ${type} "" FORCE)
endmacro()

# ------------------------------------------------------------------------------
# mbedTLS
# ------------------------------------------------------------------------------
if(QUASERVER_ENCRYPTION)
    if(NOT EXISTS ${QUASERVER_DEPENDS_DIR}/mbedtls.git/CMakeLists.txt)
        message(FATAL_ERROR
            "mbedtls submodule is missing. Run:\n"
            "  git submodule update --init --recursive")
    endif()

    quaserver_set_cache(ENABLE_PROGRAMS                    OFF BOOL)
    quaserver_set_cache(ENABLE_TESTING                     OFF BOOL)
    quaserver_set_cache(MBEDTLS_FATAL_WARNINGS             OFF BOOL)
    quaserver_set_cache(USE_STATIC_MBEDTLS_LIBRARY         ON  BOOL)
    quaserver_set_cache(USE_SHARED_MBEDTLS_LIBRARY         OFF BOOL)
    quaserver_set_cache(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON  BOOL)

    add_subdirectory(${QUASERVER_DEPENDS_DIR}/mbedtls.git
                     ${CMAKE_BINARY_DIR}/depends/mbedtls)

    # open62541 locates mbedTLS through its FindMbedTLS module. Pre-seed the
    # variables it looks for with imported wrappers around the in-tree targets
    # (imported targets do not need to be part of open62541's export set).
    foreach(_lib mbedtls mbedx509 mbedcrypto)
        add_library(QUaServerDeps::${_lib} INTERFACE IMPORTED GLOBAL)
        set_target_properties(QUaServerDeps::${_lib} PROPERTIES
            INTERFACE_LINK_LIBRARIES ${_lib})
    endforeach()
    quaserver_set_cache(MBEDTLS_INCLUDE_DIRS ${QUASERVER_DEPENDS_DIR}/mbedtls.git/include PATH)
    quaserver_set_cache(MBEDTLS_LIBRARY      QUaServerDeps::mbedtls    STRING)
    quaserver_set_cache(MBEDX509_LIBRARY     QUaServerDeps::mbedx509   STRING)
    quaserver_set_cache(MBEDCRYPTO_LIBRARY   QUaServerDeps::mbedcrypto STRING)

    quaserver_set_cache(UA_ENABLE_ENCRYPTION MBEDTLS STRING)
else()
    quaserver_set_cache(UA_ENABLE_ENCRYPTION OFF STRING)
endif()

# ------------------------------------------------------------------------------
# open62541
# ------------------------------------------------------------------------------
if(QUASERVER_NAMESPACE_FULL)
    quaserver_set_cache(UA_NAMESPACE_ZERO FULL STRING)
else()
    quaserver_set_cache(UA_NAMESPACE_ZERO REDUCED STRING)
endif()

quaserver_set_cache(UA_ENABLE_SUBSCRIPTIONS_EVENTS            ${QUASERVER_EVENTS}            BOOL)
quaserver_set_cache(UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS ${QUASERVER_ALARMS_CONDITIONS} BOOL)
quaserver_set_cache(UA_ENABLE_HISTORIZING                     ${QUASERVER_HISTORIZING}       BOOL)

quaserver_set_cache(UA_ENABLE_AMALGAMATION   OFF BOOL)
quaserver_set_cache(UA_ENABLE_PUBSUB         OFF BOOL)
quaserver_set_cache(UA_BUILD_EXAMPLES        OFF BOOL)
quaserver_set_cache(UA_BUILD_UNIT_TESTS      OFF BOOL)
quaserver_set_cache(UA_BUILD_TOOLS           OFF BOOL)
quaserver_set_cache(UA_ENABLE_DEBUG_SANITIZER OFF BOOL)
# Link against the same (dynamic) C runtime as Qt
quaserver_set_cache(UA_MSVC_FORCE_STATIC_CRT OFF BOOL)

add_subdirectory(${QUASERVER_DEPENDS_DIR}/open62541.git
                 ${CMAKE_BINARY_DIR}/depends/open62541)
